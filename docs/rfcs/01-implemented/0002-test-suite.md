# RFC 0002: Test Suite

**Status**: Implemented

## Summary

Unit test suite covering every component of the speak-anywhere daemon. Uses Catch2 v3
fetched via CMake FetchContent. Tests run without a whisper server, running daemon, or
Sway session — they exercise the code units in isolation.

## Motivation

The full skeleton (phases 1–5) is implemented, compiling, and manually verified end-to-end.
Before moving to phase 2 work (polish, edge cases, streaming), we need a test suite that
locks down the invariants of every component so regressions are caught automatically.

## Test Framework

**Catch2 v3** (v3.7.1), fetched via `FetchContent` at configure time.

Chosen over gtest for:
- Less boilerplate: `TEST_CASE`/`SECTION` vs fixture classes. Sections re-run from the
  top of the test case, giving isolated sub-tests without repetitive setup.
- Better failure messages: `REQUIRE(x == y)` decomposes arbitrary expressions.
- Built-in `GENERATE` for parameterized tests without template machinery.
- Modern C++ project that fits well with C++23.

FetchContent is used (vs system install) because Catch2 is a pure build-time dependency
with no system coupling. Production deps (PipeWire, libcurl, sqlite3) stay as system
packages because they interface with system services.

## CMake Changes

- Daemon sources (minus `main.cpp`) extracted into `speak-anywhere-lib` STATIC library
  so tests link against it without duplicating source lists.
- Single test executable `tests` linking `speak-anywhere-lib` + `Catch2::Catch2WithMain`.
- `catch_discover_tests()` for CTest integration.
- IPC client source compiled into test executable for protocol tests.

## Test Files

### `tests/test_ring_buffer.cpp` — 9 sections
Pure unit test of `RingBuffer` (header-only, no deps).

| Section | Invariant |
|---------|-----------|
| WriteAndRead | Write N bytes, read N bytes back identically |
| Wraparound | Write enough to wrap the circular buffer, data integrity preserved |
| OverflowDrops | Write more than capacity, returns partial write count |
| DrainAll | drain_all() returns all written samples as int16_t vector |
| DrainAllAlignsSamples | drain_all() rounds down to even byte boundary |
| EmptyRead | Read from empty buffer returns 0 |
| ResetClearsState | After reset(), available() is 0 |
| Available | available() tracks write_pos - read_pos correctly |
| MultipleWriteRead | Interleaved write/read cycles preserve FIFO ordering |

### `tests/test_wav_encoder.cpp` — 5 sections
Pure unit test of `wav::encode()` (header-only, no deps).

| Section | Invariant |
|---------|-----------|
| HeaderMagic | Output starts with "RIFF", contains "WAVE", "fmt ", "data" |
| HeaderSize | Total output size = 44 + (samples * 2) |
| HeaderFields | Sample rate, channels, bits_per_sample encoded correctly |
| DataIntegrity | Bytes 44+ match the input samples verbatim |
| EmptySamples | Zero-length input produces 44-byte header with data_size=0 |

### `tests/test_session.cpp` — 5 sections
State machine transitions without PipeWire.

| Section | Invariant |
|---------|-----------|
| InitialStateIdle | Freshly constructed session is Idle |
| StopWhenIdleReturnsEmpty | stop_recording() in Idle returns empty vector |
| SetTranscribingFromIdle | set_transcribing() changes state |
| SetIdleFromTranscribing | set_idle() returns to Idle |
| RecordingDurationZeroWhenIdle | recording_duration() returns 0.0 when not recording |

### `tests/test_config.cpp` — 5 sections
Config loading with temp files.

| Section | Invariant |
|---------|-----------|
| DefaultValues | Default-constructed Config has expected defaults |
| LoadFullConfig | All fields populated from valid JSON |
| LoadPartialConfig | Missing fields retain defaults |
| LoadInvalidJson | Malformed JSON falls back to defaults |
| LoadMissingFile | Non-existent path falls back to defaults |

### `tests/test_ipc_protocol.cpp` — 5 sections
IPC server and client together over a real Unix socket.

| Section | Invariant |
|---------|-----------|
| ServerStartStop | Server creates and removes socket file |
| ClientConnects | Client connects, server accepts |
| RoundTrip | Client sends JSON, server reads, server responds, client reads |
| MultipleMessages | Multiple sequential messages on same connection |
| ClientDisconnect | Server detects client disconnect gracefully |

### `tests/test_history_db.cpp` — 6 sections
SQLite operations with a temp database.

| Section | Invariant |
|---------|-----------|
| OpenCreatesFile | open() creates DB file and table |
| InsertAndRetrieve | Insert an entry, recent(1) returns it |
| LimitWorks | Insert 5, recent(2) returns only 2 |
| ReverseChronological | Most recent entry comes first |
| NullableFields | Empty strings stored as NULL, retrieved as empty |
| TimestampAutoPopulated | Entries have non-empty timestamps |

### `tests/test_agent_detector.cpp` — 4 sections
/proc walking against the live /proc filesystem.

| Section | Invariant |
|---------|-----------|
| DetectSelf | Current process found when added to known agents |
| DetectChildProcess | Fork a child, detector finds it from parent PID |
| NoMatchReturnsEmpty | Unknown agent list returns empty result |
| InvalidPidReturnsEmpty | PID 0 or -1 returns empty result |

### `tests/test_window_info.cpp` — 2 sections
WindowInfo struct.

| Section | Invariant |
|---------|-----------|
| DefaultIsEmpty | Default WindowInfo reports empty() == true |
| WithAppIdNotEmpty | WindowInfo with app_id reports empty() == false |

## Build and Run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Results

206 assertions across 8 test cases (41 sections), all passing in ~0.2s.
