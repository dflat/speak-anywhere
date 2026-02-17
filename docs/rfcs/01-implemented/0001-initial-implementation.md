# RFC 0001: Initial Implementation

**Status**: Implemented
**Phase**: 1–5 (full skeleton)

## Summary

Implements the complete speak-anywhere daemon and CLI client as a single vertical slice:
PipeWire audio capture, Whisper LAN backend, Sway window context with agent detection,
clipboard/type output, SQLite history, and Unix socket IPC.

## Design

### Thread Model

Three threads with clearly defined ownership boundaries:

1. **Main thread** — epoll event loop handling:
   - Unix socket IPC (accept, read commands, send responses)
   - Sway IPC events (window focus changes)
   - signalfd (SIGINT/SIGTERM for clean shutdown)
   - eventfd (worker thread completion notification)

2. **PipeWire thread** — managed by `pw_thread_loop`:
   - `on_process` callback dequeues PipeWire buffers
   - Copies PCM samples into lock-free SPSC ring buffer
   - No allocations in the audio path

3. **Worker thread** — `std::jthread` spawned per transcription:
   - Encodes PCM to WAV, POSTs to whisper server via libcurl
   - Writes to eventfd on completion to wake main thread
   - Joined by main thread when eventfd fires

### Data Flow

```
Mic → PipeWire stream → RingBuffer (lock-free SPSC)
    → drain_all() on stop → WAV encode → HTTP POST to whisper server
    → parse transcript → deliver output + store in SQLite
```

### Ring Buffer

Lock-free SPSC using monotonically increasing read/write positions with modular indexing.
2MB capacity (~60s at 16kHz/16-bit mono). Producer and consumer positions are on separate
cache lines (alignas(64)) to avoid false sharing.

### Audio Format

S16_LE, 16kHz, mono — Whisper's native format. PipeWire handles resampling from whatever
the hardware provides. WAV encoding is a simple 44-byte header prepended to the PCM data.

### IPC Protocol

Unix socket at `$XDG_RUNTIME_DIR/speak-anywhere.sock`. Newline-delimited JSON in both
directions. Commands: `start`, `stop`, `toggle`, `status`, `history`.

The `stop` command is special: the daemon replies asynchronously after transcription
completes. The client fd is held in a `waiting_clients_` list and the response is sent
when the worker thread signals completion via eventfd.

### Whisper Backend

Abstract `WhisperBackend` interface with `transcribe(span<int16_t>, sample_rate)` returning
`std::expected<TranscriptResult, std::string>`. Single implementation: `LanBackend` using
libcurl multipart POST. Supports two API formats:

- **whisper.cpp**: POST to `/inference` with multipart fields `file`, `temperature`, `response_format`, `language`
- **OpenAI-compatible**: POST to `/v1/audio/transcriptions` with fields `file`, `model`, `language`, `response_format`

### Sway Integration

Direct binary i3-ipc protocol implementation (no swaymsg dependency). Two socket connections:
one for synchronous queries (GET_TREE), one for event subscription (window focus events).
The event socket is registered in epoll for async delivery.

### Agent Detection

From the focused terminal's PID, recursively walks `/proc/{pid}/task/*/children` reading
`/proc/{child}/comm` to match known agent process names (claude, aider, gh, cursor).
Reads the agent's `/proc/{pid}/cwd` for working directory context.

Result: context strings like "claude code on kitty" rather than just "kitty".

### Output Methods

- **ClipboardOutput**: fork+exec `wl-copy`, pipe text to stdin
- **TypeOutput**: for GUI apps, `wtype -d 0 "text"`; for terminals, uses hybrid approach
  (clipboard + `wtype -M ctrl -M shift -k v` paste shortcut) to avoid character-by-character
  typing issues

Terminal detection is based on app_id matching known terminal emulators (kitty, alacritty,
foot, wezterm).

### SQLite History

WAL mode for concurrent access. Prepared statements for insert and query. Stores full
context: text, audio duration, processing time, app_id, window title, detected agent,
working directory, backend type.

Database location: `$XDG_DATA_HOME/speak-anywhere/history.db` (default: `~/.local/share/speak-anywhere/history.db`).

## Alternatives Considered

### swaymsg subprocess vs direct Sway IPC
Chose direct binary protocol to avoid fork+exec overhead on every window query and to
maintain persistent event subscription. The i3-ipc protocol is simple (14-byte header +
JSON payload).

### Exceptions vs std::expected
Chose `std::expected<T,E>` throughout for error handling. No exceptions are thrown or caught.
This matches the "errors are values" philosophy and avoids exception safety concerns in the
multi-threaded audio path.

### libpulse vs PipeWire
PipeWire is the native audio system on the target platform (Arch + Sway). Its
`pw_thread_loop` provides clean thread management and the stream API handles format
negotiation and resampling automatically.

## Dependencies

- libpipewire-0.3 (audio capture)
- libcurl (HTTP client)
- sqlite3 (history storage)
- nlohmann/json (JSON parsing, header-only)
- wl-clipboard (clipboard output, external binary)
- wtype (virtual typing output, optional external binary)

## Verification

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run daemon in foreground
./build/speak-anywhere --foreground --verbose

# In another terminal — test IPC
./build/sa status          # → "State: idle"
./build/sa start           # → "OK" (recording starts)
./build/sa status          # → "State: recording"
./build/sa stop            # → transcript or error if no whisper server

# Verified: agent detection shows "claude code on kitty" when run from
# a terminal with Claude Code active.
```
