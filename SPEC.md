# speak-anywhere: Speech-to-Text Daemon

## Goals

- Provide instant speech-to-text for CLI agent workflows (Claude Code, aider) on Sway/Arch Linux
- Capture microphone audio via PipeWire, send to Whisper model on LAN server
- Deliver transcript to focused application via clipboard or virtual typing
- Maintain SQLite history with rich app context (terminal + CLI agent detection)

## Non-Goals

- Cross-platform support (Linux/Sway only for v1)
- Streaming transcription (batch only for v1)
- GUI or tray icon
- Local model inference (LAN server only for v1)

## Architecture

```
[Mic] -> [PipeWire thread] -> [Ring Buffer] -> [Main thread: drain on stop]
  -> [WAV encode] -> [Whisper Backend] -> [Transcript]
  -> [Output: clipboard / type] + [SQLite history w/ Sway context]
```

### Thread Model

- **Main thread**: epoll loop — Unix socket IPC, Sway IPC events, signal handling, worker notifications
- **PipeWire thread**: `pw_thread_loop` — audio capture, writes to lock-free SPSC ring buffer
- **Worker thread**: `std::jthread` — transcription via backend (libcurl), notifies main via eventfd

### Binaries

- **`speak-anywhere`** — daemon (long-running)
- **`sa`** — CLI client (send commands, print results, exit)

## Platform Requirements

- Arch Linux with Sway compositor
- PipeWire audio system
- g++ 15+ with C++23 support
- wl-clipboard (clipboard output)
- wtype (optional, for virtual typing output)

## Dependencies

| Library | Purpose |
|---------|---------|
| libpipewire-0.3 | Audio capture |
| libcurl | HTTP to whisper server |
| sqlite3 | History database |
| nlohmann/json | Config + IPC protocol |
