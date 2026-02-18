# Journal: Solving the "Type-Writer" Problem in Wayland

**Date**: February 17, 2026  
**Topic**: Robust Text Delivery and X11 Context  
**RFC Reference**: [RFC 0003](../rfcs/01-implemented/0003-robust-output-delivery.md)

## The Problem: Metadata Fragility

In the initial implementation of `speak-anywhere`, I relied on `app_id` as the "source of truth" for whether a window was focused. If `app_id` was empty, the daemon assumed no window was focused.

I discovered that even in native Wayland applications like Brave, the `app_id` can occasionally be empty or transiently missing during specific compositor states. This meant that transcription would fail not because the browser was incompatible, but because the daemon's "vision" was too narrow.

## The Fix: Identity-Agnostic Focus

I refactored the detection logic to trust the compositor's `focused` flag regardless of whether `app_id` or `window_class` is present.

```cpp
// src/daemon/sway/window_info.hpp
bool empty() const { 
    return app_id.empty() && window_class.empty() && title.empty() && pid == 0; 
}
```

By checking for `title` and `pid` as fallbacks, we ensured that the daemon stays active as long as *any* window is focused. 

### Why the Paste Strategy won
Even for native Wayland apps, `wtype <string>` often fails because browsers like Brave have complex input pipelines that may drop "virtual" keyboard events if they arrive too quickly. `Ctrl+V` is a single, atomic event that triggers the application's internal Clipboard API—a much more reliable path than simulating hundreds of individual keystrokes.

## Why the 10ms Delay?

You might wonder why `usleep(10000)` is necessary. Wayland clipboard ownership is a negotiation. When `wl-copy` runs, it tells the compositor "I have data." If we immediately trigger `Ctrl+V`, the target application asks the compositor for data *before* the compositor has fully acknowledged `wl-copy` as the new owner. That 10ms window—well below human perception—is just enough to ensure the "handshake" is complete.

## Conclusion

By treating the clipboard as a high-speed data bus and using virtual keystrokes only as a trigger, `speak-anywhere` is now truly app-agnostic. Whether it's a native Wayland terminal or an X11 browser, the text gets where it needs to go.
