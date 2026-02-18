# RFC 0004: I/O Robustness and Signal Handling

**Status**: Implemented

## Summary

Improves the daemon's stability during system interrupts (signals) and ensures clean resource cleanup and child process management during shutdown.

## Problem

1. **Interrupted System Calls**: I/O operations like `write` and `waitpid` would fail if a signal (like `SIGINT` or `SIGCHLD`) was received during execution. This led to orphaned child processes (e.g., `wl-copy` or `wtype` remaining active) and prevented the systemd service from stopping cleanly.
2. **Abrupt Shutdown**: The daemon would exit immediately upon receiving a signal, potentially abandoning a pending transcription that was already in progress.
3. **Orphaned CGroups**: Systemd would report the service as "deactivating" indefinitely because orphaned child processes were still member of the service's control group.

## Design

### 1. Robust I/O Loops
All critical system calls are now wrapped in `EINTR` retry loops:
- `write()`: Continues writing from the last position if interrupted.
- `waitpid()`: Retries until the child process state is successfully collected.

### 2. Graceful Shutdown Sequence
The `EventLoop::run` shutdown sequence was refined:
1. Signal received (via `signalfd`).
2. Immediate stop of audio capture.
3. Check for pending transcription state.
4. If transcribing, wait for the worker thread to finish and deliver the final output before exiting.
5. Explicitly join the worker thread.

### 3. Service Management Improvements
Updated the `install.sh` script to handle stuck services more aggressively:
- Added a 5-second timeout to the standard `systemctl stop`.
- Fallback to `systemctl kill -s SIGKILL` if the graceful shutdown fails, ensuring the CGroup is cleared for a fresh installation.

## Verification

1. Started recording/transcribing.
2. Sent `SIGTERM` to the daemon.
3. Observed the daemon completing the transcription and delivering the output before exiting.
4. Verified `systemctl --user status` showed the service as inactive (clean exit) rather than stuck.
