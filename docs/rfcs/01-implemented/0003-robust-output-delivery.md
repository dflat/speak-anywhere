# RFC 0003: Robust Output Delivery for Non-Terminal Windows

**Status**: Implemented

## Summary

This RFC addresses issues with the `type` output method in non-terminal applications (specifically browsers and X11/XWayland windows) and improves window identification to avoid accidental filtering of non-Wayland applications.

## Problem

1. **Unreliable Typing**: Using `wtype` to type long strings directly into browsers or XWayland applications was unreliable. These applications often missed characters or ignored virtual keyboard input entirely.
2. **Missing X11 Context**: The Sway IPC logic only looked for `app_id` (Wayland-native). X11 windows (running via XWayland) have an empty `app_id` but provide a `window_class` in `window_properties`. This caused browsers to be treated as "empty" context, which triggered filtering or degraded output logic.
3. **Race Conditions**: Issuing a paste command immediately after a clipboard copy often resulted in the previous clipboard content being pasted because the compositor had not yet synchronized the clipboard ownership.

## Design

### 1. Unified Paste Strategy
Instead of character-by-character typing, `TypeOutput` now uses the clipboard as a robust intermediary for all applications:
- **Terminal Apps**: `wl-copy` followed by `Ctrl+Shift+V` via `wtype`.
- **General Apps**: `wl-copy` followed by `Ctrl+V` via `wtype`.

### 2. X11 Window Support
Updated `WindowInfo` and `SwayIpc` to handle X11 `window_class`:
- Extracted `window_properties.class` when `app_id` is missing.
- Updated `enrich_window_info` to use `window_class` as a fallback for the human-readable context string.
- Included `window_class` in the SQLite history database.

### 3. Synchronization Delays
Introduced a small, sub-perceptual delay (10ms) between the `wl-copy` operation and the `wtype` paste shortcut. This ensures the Wayland compositor has processed the clipboard ownership change before the target application receives the paste event.

## Verification

1. Focused a browser (Firefox/Chrome).
2. Ran `sa toggle --output type`.
3. Verified the transcript was correctly pasted into the browser's active text field.
4. Checked `sa history` to verify the `window_class` (e.g., "firefox") was correctly recorded.
