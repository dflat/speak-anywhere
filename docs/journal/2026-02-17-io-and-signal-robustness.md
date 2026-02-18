# Journal: Lessons in Signal Safety and Daemon Cleanliness

**Date**: February 17, 2026  
**Topic**: I/O Robustness and Graceful Shutdowns  
**RFC Reference**: [RFC 0004](../rfcs/01-implemented/0004-io-robustness-and-signal-handling.md)

## The Problem: The "Stuck" Service

During development, I noticed that `systemctl --user stop speak-anywhere` would often hang. Looking at the process tree, the daemon was gone, but a `wl-copy` process was still alive, holding the systemd CGroup open. 

There were two culprits:
1. **Uncollected Children**: If the daemon received a `SIGTERM` while a child was running, it would exit without calling `waitpid`.
2. **Interrupted Calls**: System calls like `write` and `waitpid` were failing with `EINTR` (Interrupted system call) because signals were arriving at the "wrong" time.

## The Fix: Robustness at the Syscall Level

### 1. Handling `EINTR`
In Linux, signals can interrupt blocking system calls. If you don't handle this, your program assumes a fatal error occurred when it was actually just a temporary interruption.

```cpp
// src/daemon/output/clipboard_output.cpp

size_t total_written = 0;
while (total_written < text.size()) {
    ssize_t n = ::write(pipefd[1], text.data() + total_written, text.size() - total_written);
    if (n < 0) {
        if (errno == EINTR) continue; // Just a signal, try again!
        // ... handle actual errors ...
    }
    total_written += static_cast<size_t>(n);
}
```

This pattern ensures that we don't drop half a transcription just because a window focus event (delivered via signal) happened during the write.

### 2. The Graceful Exit
A daemon shouldn't just "die" when told to stop. It should finish its current job. If you've just finished speaking, you want that transcript even if you're shutting down the computer.

I updated the `EventLoop` shutdown logic to check for a pending transcription:

```cpp
// src/daemon/event_loop.cpp

// Clean shutdown
if (session_.state() == SessionState::Recording) {
    audio_capture_.stop();
}

if (session_.state() == SessionState::Transcribing) {
    log("Waiting for pending transcription to complete...");
    on_transcription_complete(); // Process the result before we leave!
}
```

### 3. The "Nuclear Option" in `install.sh`
Even with perfect code, external factors can leave a service in a weird state. To make the developer experience seamless, I updated the installation script to handle stubborn processes.

```bash
# install.sh

echo "Stopping speak-anywhere.service..."
if ! timeout 5s systemctl --user stop speak-anywhere.service; then
    echo "Service failed to stop gracefully, force killing..."
    systemctl --user kill -s SIGKILL speak-anywhere.service || true
    systemctl --user stop speak-anywhere.service || true
fi
```

## Why This Matters

A speech-to-text tool is a productivity enhancer. If it hangs your system, or if you lose a 30-second recording because you closed the terminal too fast, the trust is broken. These system-level changes—handling interrupts and ensuring final delivery—are what turn a "script" into a "tool."

## Conclusion

Robustness isn't just about handling user errors; it's about respecting the lifecycle of the operating system. By handling interrupts and ensuring child processes are accounted for, we've made `speak-anywhere` a much better citizen of the Linux desktop.
