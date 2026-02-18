# RFC 0005: Platform Abstraction for Windows Portability

**Status**: Implemented

## Summary

Restructures the codebase so all platform-specific code lives behind virtual interfaces, enabling future Windows support without touching business logic.

## Problem

All daemon code was tightly coupled to Linux APIs (PipeWire, epoll, signalfd, /proc, Sway IPC, wl-copy/wtype, Unix sockets). Adding Windows support would require invasive changes throughout the business logic layer.

## Design

### Architecture Split

The codebase is now three layers:

1. **Portable core** -- business logic and cross-platform libraries (ring buffer, WAV encoder, config, session state machine, SQLite history, whisper HTTP backend).
2. **Platform interfaces** -- virtual base classes in `src/daemon/platform/` defining the contract each platform must fulfill.
3. **Platform implementations** -- concrete subclasses in `src/daemon/platform/linux/` containing all Linux-specific code.

### EventLoop Decomposition

The monolithic `EventLoop` (393 lines) was split into:

- **`DaemonCore`** (portable) -- owns Session, HistoryDb, WhisperBackend. Contains all command handlers, transcription orchestration, and output delivery. References platform interfaces without knowing which implementations back them. Receives an `OutputFactory` callback and a `NotifyCallback` so it can create outputs and signal the event loop without platform coupling.
- **`LinuxEventLoop`** (platform-specific) -- owns all platform implementations and `DaemonCore`. Contains epoll/signalfd/eventfd setup and the fd-dispatch loop. Calls into `DaemonCore` for all business logic.

### Platform Interfaces

| Interface | Linux Implementation | Previous Name |
|-----------|---------------------|---------------|
| `AudioCapture` | `PipeWireCapture` | `AudioCapture` |
| `WindowManager` | `SwayWindowManager` | `SwayIpc` |
| `ProcessDetector` | `ProcfsDetector` | `AgentDetector` |
| `IpcServer` | `UnixSocketServer` | `IpcServer` |
| `IpcClient` (client) | `UnixSocketClient` | `IpcClient` |
| `platform::config_dir/data_dir/ipc_endpoint` | `linux_paths.cpp` | inline in config.cpp/event_loop.cpp |
| `platform::daemonize` | `linux_daemonize.cpp` | static function in main.cpp |
| `OutputMethod` (unchanged) | `WaylandClipboardOutput`, `WaylandTypeOutput` | `ClipboardOutput`, `TypeOutput` |

### CMake Changes

PipeWire is now Linux-only. Source lists are platform-conditional:
```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_SOURCES src/daemon/platform/linux/*.cpp)
    set(PLATFORM_LINK_LIBS PkgConfig::PIPEWIRE)
endif()
```

libcurl, sqlite3, and nlohmann/json remain global (portable).

### Test Improvements

`test_session.cpp` now uses a `MockAudioCapture` instead of the real PipeWire-based class. This removes the PipeWire link dependency from the session test and makes it a proper unit test.

## Directory Structure

```
src/daemon/
    daemon_core.hpp/cpp           Portable business logic
    platform/
        audio_capture.hpp         Virtual interfaces
        window_manager.hpp
        process_detector.hpp
        ipc_server.hpp
        platform_paths.hpp
        daemonizer.hpp
        linux/
            pipewire_capture.*    Linux implementations
            sway_window_manager.*
            procfs_detector.*
            unix_socket_server.*
            linux_event_loop.*
            linux_paths.cpp
            linux_daemonize.cpp
            wayland_clipboard_output.*
            wayland_type_output.*
src/client/
    platform/
        ipc_client.hpp            Virtual interface
        linux/
            unix_socket_client.*  Linux implementation
```

## Known Future Seams

1. `event_fd()` / `server_fd()` return `int` -- on Windows these would be `HANDLE`. Acceptable because the event loop consuming these is also platform-specific.
2. Terminal detection (app_id string matching) is in `DaemonCore`. Windows would need different detection logic, which would require moving this to the platform layer.
3. `sway/window_info.hpp` field `app_id` is Wayland terminology. The struct already has `window_class` for X11/Windows.

## Verification

1. `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)` -- clean build
2. `ctest --test-dir build --output-on-failure` -- 8/8 tests pass
3. Manual smoke test: `./build/speak-anywhere -f -v` + `./build/sa toggle`
