# Journal: Solving the "Type-Writer" Problem in Wayland

**Date**: February 17, 2026  
**Topic**: Robust Text Delivery and X11 Context  
**RFC Reference**: [RFC 0003](../rfcs/01-implemented/0003-robust-output-delivery.md)

## The Problem: When "Typing" Isn't Enough

In the initial implementation of `speak-anywhere`, I relied heavily on `wtype` to simulate a virtual keyboard. It worked beautifully in simple terminal emulators, but the moment I tried to use it in a browser or a complex Electron app, things fell apart. Characters would drop, the speed was inconsistent, and occasionally, it would trigger unintended browser shortcuts.

Furthermore, I noticed that transcription simply wouldn't start if I was focused on a browser. Sway's IPC would return an empty `app_id` for XWayland windows, and my logic was treating that as a "null" context.

## The Fix: Moving to Unified Pasting

Wayland's security model (rightfully) prevents one application from injecting state into another. To get text into a browser reliably, we have to simulate a user's intent. The most universal intent is "Paste."

### 1. Unified Output Strategy
I refactored `TypeOutput` to stop trying to be a fast typist. Instead, it now acts as a clipboard orchestrator.

```cpp
// src/daemon/output/type_output.cpp

std::expected<void, std::string> TypeOutput::general_paste(const std::string& text) {
    // 1. Hand the text to wl-copy
    ClipboardOutput clip;
    auto res = clip.deliver(text);
    if (!res) return res;

    // 2. Give the compositor a moment to breathe
    ::usleep(10000); // 10ms synchronization delay

    // 3. Trigger the universal shortcut
    pid_t pid = ::fork();
    if (pid == 0) {
        ::execlp("wtype", "wtype", "-M", "ctrl", "-k", "v", nullptr);
        ::_exit(127);
    }
    // ... wait for pid ...
}
```

By using `wl-copy` to load the clipboard and `wtype` to trigger `Ctrl+V`, we gain atomicity. The target application handles the clipboard retrieval in one internal event, which is significantly more robust than processing 500 individual key-down/key-up events.

### 2. Identifying X11 Windows
To fix the "silent filtering" of browsers, I had to dig deeper into the Sway tree. X11 applications running through XWayland don't have an `app_id`, but they do have a `window_class` nested inside `window_properties`.

```cpp
// src/daemon/sway/ipc.cpp

if (node.value("focused", false)) {
    WindowInfo info;
    info.app_id = node.value("app_id", "");
    
    // Fallback for X11/XWayland
    if (info.app_id.empty() && node.contains("window_properties")) {
        info.window_class = node["window_properties"].value("class", "");
    }
    // ...
}
```

## Why the 10ms Delay?

You might wonder why `usleep(10000)` is necessary. Wayland clipboard ownership is a negotiation. When `wl-copy` runs, it tells the compositor "I have data." If we immediately trigger `Ctrl+V`, the target application asks the compositor for data *before* the compositor has fully acknowledged `wl-copy` as the new owner. That 10ms window—well below human perception—is just enough to ensure the "handshake" is complete.

## Conclusion

By treating the clipboard as a high-speed data bus and using virtual keystrokes only as a trigger, `speak-anywhere` is now truly app-agnostic. Whether it's a native Wayland terminal or an X11 browser, the text gets where it needs to go.
