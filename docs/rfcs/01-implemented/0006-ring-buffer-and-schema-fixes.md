# RFC 0006: Ring Buffer Overflow and Database Schema Migration

**Status**: Implemented

## Summary

Fixes two pre-existing bugs: (1) audio recordings silently truncated at 65.5 seconds due to undersized ring buffer, and (2) history database failing on existing installations due to missing schema migration.

## Problem 1: Audio Truncation at 65.5s

Recordings longer than ~65.5 seconds were silently truncated. The daemon reported the truncated duration with no error, so the user had no indication that audio was lost.

**Root cause**: The ring buffer was hardcoded to 2MB (`2 * 1024 * 1024` bytes). At 16kHz mono 16-bit audio (32,000 bytes/sec), this holds exactly 65.536 seconds. The ring buffer's `write()` method drops data when full (returns 0), and since the buffer is only drained at `stop_recording()`, all audio past 65.5s was silently discarded.

The config had `max_seconds = 120` (2 minutes), but the ring buffer could only hold half that.

**Fix**: Replaced the hardcoded `ring_buffer_bytes` field with a computed method:
```cpp
size_t ring_buffer_bytes() const {
    return static_cast<size_t>(max_seconds) * sample_rate * sizeof(int16_t);
}
```
For the default 120s at 16kHz, this yields 3,840,000 bytes (~3.7MB). The buffer now always matches `max_seconds`, even if the user changes it in config.

## Problem 2: History Database Schema Migration

On existing installations, the daemon failed to start history recording:
```
db: prepare insert failed: table transcriptions has no column named window_class
Warning: history DB failed to open, history disabled
```

**Root cause**: `CREATE TABLE IF NOT EXISTS` does not add new columns to an existing table. When the schema was expanded (adding `window_class`, `app_id`, `agent`, etc.), existing databases kept their old schema and all INSERT/SELECT statements referencing the new columns failed.

**Fix**: Added `migrate_schema()` called after `create_tables()`. It runs `ALTER TABLE ADD COLUMN` for each column that may be missing. SQLite silently errors if the column already exists, which we ignore -- this makes the migration idempotent.

```cpp
void HistoryDb::migrate_schema() {
    static const char* migrations[] = {
        "ALTER TABLE transcriptions ADD COLUMN app_context TEXT",
        "ALTER TABLE transcriptions ADD COLUMN app_id TEXT",
        "ALTER TABLE transcriptions ADD COLUMN window_class TEXT",
        // ... etc
    };
    for (const char* sql : migrations) {
        sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
    }
}
```

## Verification

1. `cmake --build build -j$(nproc)` -- clean build
2. `ctest --test-dir build --output-on-failure` -- 8/8 tests pass
3. Ring buffer: default config now allocates 3.7MB (120s capacity) instead of 2MB (65.5s)
4. DB migration: existing databases with missing columns are upgraded in-place on next daemon start
