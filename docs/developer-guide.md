# Developer Guide

A walkthrough of the speak-anywhere codebase for developers who may be more familiar
with C than C++, or who haven't worked with Linux system APIs like epoll, PipeWire,
or Unix domain sockets before.

## Table of Contents

1. [The Big Picture](#the-big-picture)
2. [How the Pieces Fit Together](#how-the-pieces-fit-together)
3. [C++ Concepts Used in This Codebase](#c-concepts-used-in-this-codebase)
4. [The Ring Buffer](#the-ring-buffer)
5. [Audio Capture with PipeWire](#audio-capture-with-pipewire)
6. [The WAV Encoder](#the-wav-encoder)
7. [The Session State Machine](#the-session-state-machine)
8. [The Event Loop and epoll](#the-event-loop-and-epoll)
9. [Unix Domain Socket IPC](#unix-domain-socket-ipc)
10. [The CLI Client](#the-cli-client)
11. [The Whisper LAN Backend](#the-whisper-lan-backend)
12. [Sway IPC: Talking to the Window Manager](#sway-ipc-talking-to-the-window-manager)
13. [Agent Detection: Walking /proc](#agent-detection-walking-proc)
14. [Output Methods: Getting Text to the User](#output-methods-getting-text-to-the-user)
15. [SQLite History](#sqlite-history)
16. [Configuration](#configuration)
17. [Daemon Startup and Daemonization](#daemon-startup-and-daemonization)
18. [Thread Safety Summary](#thread-safety-summary)

---

## The Big Picture

speak-anywhere is two programs:

- **`speak-anywhere`** (the daemon) — a long-running process that captures audio from
  your microphone, sends it to a speech-to-text server, and delivers the result.
- **`sa`** (the client) — a short-lived CLI tool that tells the daemon what to do
  ("start recording", "stop recording", "show history") and prints the result.

They communicate over a **Unix domain socket** using newline-delimited JSON messages.

The daemon has three threads:

```
Main thread                PipeWire thread          Worker thread
(epoll event loop)         (audio capture)          (transcription)
     |                          |                        |
     |   sa sends "start"       |                        |
     |<-----(IPC)               |                        |
     |   start PipeWire ------->|                        |
     |                          |---> ring buffer         |
     |                          |---> ring buffer         |
     |   sa sends "stop"        |---> ring buffer         |
     |<-----(IPC)               |                        |
     |   stop PipeWire -------->|                        |
     |   drain ring buffer      |                        |
     |   spawn worker ---------------------------------->|
     |                                                   |  encode WAV
     |                                                   |  HTTP POST
     |                                                   |  parse JSON
     |   eventfd fires  <----(eventfd)-------------------|
     |   join worker                                     |
     |   deliver output                                  |
     |   store in SQLite                                 |
     |   reply to sa                                     |
```

## How the Pieces Fit Together

Here's every source file and what it does:

```
src/daemon/
  main.cpp                   Entry point. Parses args, loads config, daemonizes, runs event loop.
  event_loop.hpp/.cpp        The orchestrator. Owns all components. Runs the epoll loop.
  audio_capture.hpp/.cpp     Wraps PipeWire to record from the microphone.
  ring_buffer.hpp            Lock-free queue between the PipeWire thread and main thread.
  wav_encoder.hpp            Converts raw PCM samples to a .wav file in memory.
  session.hpp/.cpp           State machine: Idle → Recording → Transcribing → Idle.
  config.hpp/.cpp            Loads JSON config file.
  ipc_server.hpp/.cpp        Accepts connections from `sa` clients on a Unix socket.

  whisper/
    backend.hpp              Abstract interface: "give me audio, I'll give you text."
    lan_backend.hpp/.cpp     Concrete implementation: HTTP POST to a whisper.cpp server.

  sway/
    window_info.hpp          Plain data struct: app_id, title, pid, agent, working_dir, context.
    ipc.hpp/.cpp             Talks to the Sway window manager to find the focused window.
    agent_detector.hpp/.cpp  Walks Linux /proc to detect CLI agents (claude, aider, etc.).

  output/
    output.hpp               Abstract interface: "deliver this text to the user."
    clipboard_output.hpp/.cpp  Copy text to clipboard via wl-copy.
    type_output.hpp/.cpp       Type text via wtype, or clipboard+paste for terminals.

  storage/
    history_db.hpp/.cpp      SQLite database for transcription history.

src/client/
  main.cpp                   Entry point for `sa`. Parses args, sends JSON, prints response.
  ipc_client.hpp/.cpp        Connects to the daemon's Unix socket.
```

## C++ Concepts Used in This Codebase

If you're coming from C, here's a quick primer on the C++ features you'll see.

### RAII (Resource Acquisition Is Initialization)

In C, you `malloc` then `free`, `open` then `close`, etc. In C++, constructors acquire
resources and destructors release them. When an object goes out of scope, its destructor
runs automatically.

```cpp
// C style:
int fd = open("file", O_RDONLY);
// ... use fd ...
close(fd);  // easy to forget, especially on error paths

// C++ style (this codebase):
class IpcClient {
    int fd_ = -1;
public:
    ~IpcClient() {
        if (fd_ >= 0) ::close(fd_);  // automatic cleanup
    }
};
```

Every class in this codebase that owns a resource (file descriptor, PipeWire stream,
SQLite handle) cleans up in its destructor. You'll never see a manual `close()` in
the caller — destruction handles it.

### std::expected<T, E> (C++23)

This is like Rust's `Result<T, E>`. A function returns either a success value or an
error value, and the caller must check which one it got.

```cpp
// Instead of returning -1 or setting errno:
std::expected<TranscriptResult, std::string> transcribe(audio, sample_rate);

// Usage:
auto result = backend->transcribe(audio, 16000);
if (result.has_value()) {
    auto& transcript = result.value();  // success path
} else {
    auto& error_msg = result.error();   // error path
}

// Returning an error:
return std::unexpected("something went wrong");
```

This is used in the whisper backend and output methods.

### std::span<T> (C++20)

A non-owning view of a contiguous sequence — like passing a pointer and length,
but as a single object.

```cpp
// C style:
void process(const int16_t* data, size_t count);

// C++ style:
void process(std::span<const int16_t> data);
// data.data() gives the pointer, data.size() gives the count
```

Used in `WhisperBackend::transcribe()` and `wav::encode()`.

### std::jthread (C++20)

Like `pthread_create` but with automatic joining. When a `std::jthread` is destroyed,
it automatically requests the thread to stop and waits for it to finish. No risk of
forgetting to join.

```cpp
std::jthread worker_([this](std::stop_token) {
    // do work on background thread
});
// when worker_ is destroyed or reassigned, the thread is joined automatically
```

### std::atomic<T>

The C++ equivalent of C11's `_Atomic`. Provides thread-safe reads and writes to
a variable without needing a mutex.

```cpp
std::atomic<bool> capturing_{false};

// Thread 1:
capturing_.store(true, std::memory_order_release);

// Thread 2:
if (capturing_.load(std::memory_order_relaxed)) { ... }
```

### std::format and std::println (C++23)

Like `printf` but type-safe and with `{}` placeholders instead of `%d`/`%s`:

```cpp
std::println(stderr, "audio: stream connect failed: {}", spa_strerror(ret));
// equivalent to: fprintf(stderr, "audio: stream connect failed: %s\n", spa_strerror(ret));
```

### The :: prefix on system calls

You'll see `::close(fd)`, `::fork()`, `::write()`, etc. The `::` prefix means
"call the function from the global namespace" — i.e., the C library function, not
any C++ method that might have the same name. This avoids ambiguity when a class
has a method called `close()` and you want to call POSIX `close()`.

### Designated initializers

```cpp
epoll_event ev{.events = EPOLLIN, .data = {.fd = fd}};
```

This is C99's designated initializer syntax, also available in C++20. It lets you
name the fields you're initializing.

---

## The Ring Buffer

**File**: `ring_buffer.hpp`

The ring buffer is the bridge between two threads: the PipeWire audio thread (producer)
writes raw microphone samples into it, and the main thread (consumer) reads them out
when recording stops.

### Why lock-free?

The PipeWire audio thread runs in a real-time context. If it tries to lock a mutex and
another thread holds it, the audio thread stalls. Audio hardware doesn't wait — you
get glitches (xruns). A lock-free design means neither thread ever blocks the other.

### How it works

Picture a circular buffer with two cursors:

```
Buffer (capacity = 8):
[A][B][C][D][ ][ ][ ][ ]
          ^              ^
          write_pos=4    read_pos=0

Available to read: write_pos - read_pos = 4 bytes
Available to write: capacity - (write_pos - read_pos) = 4 bytes
```

Key insight: `write_pos_` and `read_pos_` are **monotonically increasing** — they
never wrap around. Instead, we use modular arithmetic (`pos % capacity_`) to find the
actual offset into the buffer. This avoids the classic ring buffer ambiguity of "is the
buffer full or empty when read == write?"

```cpp
size_t offset = w % capacity_;
```

### Memory ordering

The two atomics use specific **memory orderings** that are worth understanding:

```cpp
// Writer:
size_t w = write_pos_.load(std::memory_order_relaxed);   // only I write this
size_t r = read_pos_.load(std::memory_order_acquire);     // need to see reader's updates
// ... write data ...
write_pos_.store(w + to_write, std::memory_order_release); // publish to reader
```

- **relaxed**: no ordering constraints. Fine for reading your own variable since
  you're the only writer.
- **acquire**: "make sure I see all writes that happened before the other thread's
  release." The writer needs to see where the reader has gotten to.
- **release**: "make sure all my preceding writes are visible before this store
  becomes visible." The reader needs to see the data we just wrote, not just the
  updated position.

These are the weakest orderings that are correct for SPSC (single-producer,
single-consumer). A `std::mutex` would do the same thing but much slower.

### Cache line alignment

```cpp
alignas(64) std::atomic<size_t> write_pos_{0};
alignas(64) std::atomic<size_t> read_pos_{0};
```

The `alignas(64)` ensures each atomic variable sits on its own 64-byte cache line.
Without this, the CPU cores sharing these variables would constantly invalidate each
other's caches (called **false sharing**), even though they're writing to different
variables. This is a standard optimization for concurrent data structures.

### drain_all()

When recording stops, the main thread calls `drain_all()` to read every available
sample out of the ring buffer in one shot:

```cpp
std::vector<int16_t> drain_all() {
    // ...
    avail &= ~size_t(1);  // round down to even byte count (16-bit samples = 2 bytes)
    // ...
}
```

The bitmask `~size_t(1)` clears the lowest bit, ensuring we read a whole number of
16-bit samples (each sample is 2 bytes). Without this, we could read half a sample
and corrupt the audio.

---

## Audio Capture with PipeWire

**Files**: `audio_capture.hpp`, `audio_capture.cpp`

PipeWire is the modern Linux audio system (replacing PulseAudio and JACK). It manages
audio routing between applications and hardware.

### Key PipeWire concepts

- **Thread loop** (`pw_thread_loop`): PipeWire runs its own event loop on a dedicated
  thread. You create it, start it, and it handles all the timing and callbacks.
- **Stream** (`pw_stream`): A connection to an audio source (like your microphone).
  You specify the format you want and PipeWire handles resampling and format conversion.
- **Callbacks**: You register functions that PipeWire calls when events happen —
  when data arrives (`on_process`) or when the stream state changes (`on_state_changed`).

### Setup (AudioCapture::start)

```cpp
auto* props = pw_properties_new(
    PW_KEY_MEDIA_TYPE, "Audio",
    PW_KEY_MEDIA_CATEGORY, "Capture",
    PW_KEY_MEDIA_ROLE, "Communication",
    PW_KEY_NODE_NAME, "speak-anywhere",
    // ...
);
```

These properties tell PipeWire what kind of stream this is. `PW_KEY_MEDIA_ROLE` set to
"Communication" tells the audio system this is voice input, which can affect routing
and processing (e.g., enabling noise cancellation if available).

```cpp
auto info = SPA_AUDIO_INFO_RAW_INIT(
    .format = SPA_AUDIO_FORMAT_S16_LE,
    .rate = sample_rate_,   // 16000
    .channels = 1
);
```

We request S16_LE (signed 16-bit little-endian), 16kHz, mono. This is Whisper's native
input format. PipeWire automatically resamples from whatever the hardware actually
provides (typically 48kHz stereo).

The flags on `pw_stream_connect`:
- `AUTOCONNECT`: automatically connect to the default microphone
- `MAP_BUFFERS`: map buffer memory so we can read it directly (vs. copying)
- `RT_PROCESS`: call our `on_process` in the real-time thread (lowest latency)

### The audio callback (on_process)

```cpp
void AudioCapture::on_process(void* userdata) {
    auto* self = static_cast<AudioCapture*>(userdata);

    auto* buf = pw_stream_dequeue_buffer(self->stream_);
    if (!buf) return;

    auto* d = &buf->buffer->datas[0];
    auto* data = static_cast<const uint8_t*>(d->data) + d->chunk->offset;
    size_t size = d->chunk->size;

    if (self->capturing_.load(std::memory_order_relaxed)) {
        self->ring_buf_.write(data, size);
    }

    pw_stream_queue_buffer(self->stream_, buf);
}
```

This runs on PipeWire's real-time thread, typically every few milliseconds. The pattern:

1. **Dequeue** a buffer from PipeWire (it filled it with microphone data)
2. **Copy** the audio data into our ring buffer
3. **Re-queue** the buffer back to PipeWire (it can fill it again)

Note: we pass `this` as the `userdata` pointer, then `static_cast` it back. This
is the standard C callback pattern — PipeWire's API is C, so it uses `void*`
userdata pointers. The `static_cast` is safe because we know the type.

This callback does almost nothing: one atomic load, one memcpy, no allocations,
no locks. This is essential for real-time audio. If this function takes too long
or blocks on a lock, the audio hardware buffer overflows and you lose samples.

### Shutdown ordering

```cpp
void AudioCapture::stop() {
    capturing_.store(false, std::memory_order_release);  // 1. stop writing to ring buffer
    pw_thread_loop_stop(loop_);   // 2. stop the PipeWire thread
    pw_stream_destroy(stream_);   // 3. destroy the stream
    pw_thread_loop_destroy(loop_); // 4. destroy the thread loop
}
```

The order matters. We set `capturing_` to false first so the callback (which may
still fire once or twice as the thread loop winds down) won't write more data.
Then we stop the loop, then destroy objects in reverse order of creation.

---

## The WAV Encoder

**File**: `wav_encoder.hpp`

This converts raw PCM audio samples into a WAV file in memory (a `vector<uint8_t>`).
The WAV format is simple — a 44-byte header followed by the raw sample data.

### The WAV header structure

```
Bytes 0-3:   "RIFF"              (file type marker)
Bytes 4-7:   file_size - 8       (remaining file size)
Bytes 8-11:  "WAVE"              (format identifier)
Bytes 12-15: "fmt "              (format chunk marker)
Bytes 16-19: 16                  (format chunk size, always 16 for PCM)
Bytes 20-21: 1                   (audio format: 1 = PCM, uncompressed)
Bytes 22-23: 1                   (number of channels: mono)
Bytes 24-27: 16000               (sample rate)
Bytes 28-31: 32000               (byte rate = sample_rate * channels * bits/8)
Bytes 32-33: 2                   (block align = channels * bits/8)
Bytes 34-35: 16                  (bits per sample)
Bytes 36-39: "data"              (data chunk marker)
Bytes 40-43: data_size           (number of audio data bytes)
Bytes 44+:   raw PCM samples     (the actual audio)
```

The encoder uses a lambda trick to build this sequentially:

```cpp
auto w = [&out, pos = size_t(0)](const void* data, size_t len) mutable {
    std::memcpy(out.data() + pos, data, len);
    pos += len;
};
```

This creates a small "writer" function that keeps track of its position via
a captured `pos` variable. The `mutable` keyword allows the lambda to modify
its captured `pos` (lambdas are const by default in C++).

---

## The Session State Machine

**Files**: `session.hpp`, `session.cpp`

The session tracks what the daemon is currently doing:

```
    ┌──────┐   start_recording()   ┌───────────┐   stop_recording()   ┌──────────────┐
    │ Idle │ ────────────────────>  │ Recording │ ───────────────────>  │ Transcribing │
    └──────┘                       └───────────┘                       └──────────────┘
       ^                                                                      │
       └──────────────────────── set_idle() ──────────────────────────────────┘
```

- **Idle**: nothing happening, ready to record
- **Recording**: PipeWire is capturing audio into the ring buffer
- **Transcribing**: audio has been sent to the whisper server, waiting for result

The session also captures the **window context** at the moment recording starts.
This is important — if you start recording in a terminal running Claude Code, switch
to Firefox while talking, then stop — the transcript should be attributed to Claude
Code, not Firefox. Snapshot at start, not at stop.

---

## The Event Loop and epoll

**Files**: `event_loop.hpp`, `event_loop.cpp`

This is the heart of the daemon. It ties everything together.

### What is epoll?

`epoll` is a Linux kernel API for efficiently waiting on multiple file descriptors.
In C, you might use `select()` or `poll()` — `epoll` is the high-performance
successor. The idea: register file descriptors you're interested in, then call
`epoll_wait()` which blocks until one or more of them have data ready.

```cpp
// Create an epoll instance:
int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

// Register a file descriptor:
epoll_event ev{.events = EPOLLIN, .data = {.fd = some_fd}};
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, some_fd, &ev);

// Wait for events:
epoll_event events[16];
int n = epoll_wait(epoll_fd, events, 16, -1);  // -1 = block forever
for (int i = 0; i < n; i++) {
    int fd = events[i].data.fd;
    // fd is ready to read
}
```

### What file descriptors does the event loop watch?

The daemon registers **four** kinds of FDs with epoll:

1. **signal_fd** — for catching Ctrl-C (SIGINT) and kill (SIGTERM) cleanly
2. **ipc_server_.server_fd()** — for new client connections
3. **worker_event_fd** — for "transcription finished" notifications
4. **sway_ipc_.event_fd()** — for "window focus changed" notifications
5. **client FDs** — one per connected `sa` client (added dynamically)

### signalfd: Signals as file descriptors

Normally, Unix signals (like SIGINT from Ctrl-C) are delivered asynchronously and
interrupt whatever code is running. This is problematic for an event loop because
you can't safely do much work inside a signal handler.

`signalfd` converts signals into file descriptor events. First, we block the signals
from being delivered normally:

```cpp
sigset_t mask;
sigemptyset(&mask);
sigaddset(&mask, SIGINT);
sigaddset(&mask, SIGTERM);
sigprocmask(SIG_BLOCK, &mask, nullptr);  // block normal delivery
```

Then create a file descriptor that becomes readable when those signals arrive:

```cpp
signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
```

Now in the epoll loop, SIGINT is just another readable FD. No signal handler races,
no `volatile sig_atomic_t` flags, no async-signal-safety worries.

### eventfd: Thread notification

The worker thread (which does transcription on a background thread) needs to tell
the main thread "I'm done." We can't just set a flag — the main thread is blocked
in `epoll_wait`. We need to wake it up.

`eventfd` creates a file descriptor backed by a kernel counter. Writing an integer
to it makes it readable. Reading from it consumes the value.

```cpp
// Create:
worker_event_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

// Worker thread signals completion:
uint64_t val = 1;
write(worker_event_fd_, &val, sizeof(val));

// Main thread wakes up from epoll_wait, reads:
uint64_t val;
read(worker_event_fd_, &val, sizeof(val));
on_transcription_complete();
```

This is the standard pattern for cross-thread notification in an epoll-based server.

### CLOEXEC: Why is it everywhere?

You'll see `SOCK_CLOEXEC`, `EPOLL_CLOEXEC`, `EFD_CLOEXEC`, `SFD_CLOEXEC` on most
file descriptor creation calls. The `CLOEXEC` flag means "close this fd if we exec
a child process." Without it, when we `fork()`+`exec()` to run `wl-copy` or `wtype`,
the child would inherit all our open file descriptors (sockets, epoll, etc.), which
is both a resource leak and a security concern.

### The main loop

```cpp
while (running_.load(std::memory_order_relaxed)) {
    int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;

        if (fd == signal_fd_) { /* shut down */ }
        else if (fd == ipc_server_.server_fd()) { /* accept new client */ }
        else if (fd == worker_event_fd_) { /* transcription done */ }
        else if (fd == sway_ipc_.event_fd()) { /* window focus changed */ }
        else { /* must be a client fd — read command */ }
    }
}
```

The pattern is simple: wait, then dispatch based on which FD woke us up. The daemon
spends virtually all its time blocked in `epoll_wait`, using zero CPU.

### Deferred responses

Most commands get an immediate response. But `stop` is special: the daemon needs to
transcribe the audio before it can reply with the transcript text. Rather than blocking
the client, the daemon:

1. Starts the worker thread
2. Returns `{"status": "transcribing"}` internally
3. Adds the client FD to `waiting_clients_`
4. When the worker finishes (eventfd fires), sends the real response to all waiting clients

This means `sa stop` blocks until transcription completes (30 seconds of polling
in `IpcClient::recv`), but the daemon itself stays responsive to other commands
like `sa status`.

---

## Unix Domain Socket IPC

**Files**: `ipc_server.hpp/.cpp` (daemon side), `ipc_client.hpp/.cpp` (client side)

### What are Unix domain sockets?

Unix domain sockets are like TCP sockets, but for communication between processes
on the same machine. Instead of an IP address and port, they use a filesystem path.

```
/run/user/1000/speak-anywhere.sock
```

They're faster than TCP (no network stack overhead) and support features like
file descriptor passing (which we don't use here, but could).

### Server setup

The daemon creates the server socket in `IpcServer::start()`:

```cpp
server_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

sockaddr_un addr{};
addr.sun_family = AF_UNIX;
strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

bind(server_fd_, (sockaddr*)&addr, sizeof(addr));
listen(server_fd_, 4);
```

This is exactly the same `socket` → `bind` → `listen` dance as TCP, just with
`AF_UNIX` instead of `AF_INET` and a filesystem path instead of an IP:port.

The `SOCK_NONBLOCK` flag is important: without it, `accept()` and `recv()` would
block the thread. With it, they return immediately with `EAGAIN` if there's nothing
to do, which is what we want in an epoll loop.

### The protocol

Messages are **newline-delimited JSON**. This is perhaps the simplest possible wire
protocol:

```
Client sends:  {"cmd":"start","output":"clipboard"}\n
Server sends:  {"status":"ok","message":"recording"}\n
```

Each side reads until `\n`, parses the JSON, acts on it. The `\n` delimiter is
what makes it possible to know when a complete message has arrived — without it,
you'd need a length prefix or some other framing.

### Per-client buffering

TCP (and Unix stream sockets) can deliver partial messages. If a client sends a
100-byte JSON message, `recv()` might return only 60 bytes the first time. The
server keeps a per-client buffer:

```cpp
struct ClientBuffer {
    int fd;
    std::string buf;
};
```

Each `recv()` appends to the buffer. Only when we find a `\n` do we extract and
parse the message.

---

## The CLI Client

**Files**: `src/client/main.cpp`, `ipc_client.hpp/.cpp`

The `sa` binary is intentionally simple. It:

1. Parses command-line arguments
2. Builds a JSON command
3. Connects to the daemon's Unix socket
4. Sends the command
5. Waits for a response (with a 30-second timeout using `poll()`)
6. Prints the response
7. Exits

### poll() for timeouts

`IpcClient::recv()` uses `poll()` (not `epoll` — it's only watching one FD)
to wait for data with a timeout:

```cpp
pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
int ret = poll(&pfd, 1, timeout_ms);
if (ret <= 0) return false;  // timeout or error
```

This prevents the client from hanging forever if the daemon crashes during
transcription.

---

## The Whisper LAN Backend

**Files**: `whisper/backend.hpp`, `whisper/lan_backend.hpp/.cpp`

### The interface

```cpp
class WhisperBackend {
    virtual std::expected<TranscriptResult, std::string>
        transcribe(std::span<const int16_t> audio, uint32_t sample_rate) = 0;
};
```

The `= 0` makes this a **pure virtual function** — like an interface in Java or a
trait in Rust. You can't instantiate `WhisperBackend` directly; you must create a
concrete subclass like `LanBackend`. This makes it easy to add other backends later
(e.g., a local whisper.cpp backend, or OpenAI's cloud API) without changing the
rest of the code.

### libcurl multipart POST

The LAN backend encodes the audio as WAV, then sends it as an HTTP multipart form
POST — the same format a web browser uses when you upload a file through an
`<input type="file">` form.

```cpp
curl_mime* mime = curl_mime_init(curl);

// Add the WAV file
curl_mimepart* part = curl_mime_addpart(mime);
curl_mime_name(part, "file");
curl_mime_data(part, wav_data.data(), wav_data.size());
curl_mime_filename(part, "audio.wav");
curl_mime_type(part, "audio/wav");

// Add other form fields
part = curl_mime_addpart(mime);
curl_mime_name(part, "temperature");
curl_mime_data(part, "0.0", CURL_ZERO_TERMINATED);
```

This is libcurl's "mime" API. Each `curl_mime_addpart` adds a form field.
`CURL_ZERO_TERMINATED` tells libcurl to determine the string length by looking
for a null terminator (like C's `strlen`), while for the WAV data we pass the
explicit size since binary data may contain null bytes.

### The write callback

```cpp
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* resp = static_cast<std::string*>(userdata);
    resp->append(ptr, size * nmemb);
    return size * nmemb;
}
```

libcurl doesn't give you the response body as a string — it calls your callback
function, possibly multiple times, as data arrives. You accumulate the pieces.
The return value must equal `size * nmemb` or libcurl treats it as an error.
This is a C API pattern; most C libraries use callbacks with `void*` userdata.

### Worker thread integration

The LAN backend runs on a worker thread (not the main thread) because HTTP requests
can take seconds. The flow:

```cpp
// In EventLoop::start_transcription():
worker_ = std::jthread([this, audio, context, output_method, sample_rate](std::stop_token) {
    auto result = backend_->transcribe(audio, sample_rate);
    worker_result_ = WorkerResult{ .result = result, .context = context, ... };

    uint64_t val = 1;
    write(worker_event_fd_, &val, sizeof(val));  // wake up main thread
});
```

The lambda captures the audio data by value (it's moved in), so the worker thread
owns its copy. When done, it writes to `worker_event_fd_` which wakes the main
thread's epoll loop.

---

## Sway IPC: Talking to the Window Manager

**Files**: `sway/ipc.hpp`, `sway/ipc.cpp`

Sway (and i3) expose an IPC interface over a Unix domain socket. The socket path
is in the `$SWAYSOCK` environment variable.

### The binary protocol

Unlike our JSON-over-newlines IPC, Sway uses a compact binary protocol:

```
Bytes 0-5:   "i3-ipc"     (magic bytes)
Bytes 6-9:   payload_len  (uint32_t, little-endian)
Bytes 10-13: message_type (uint32_t, little-endian)
Bytes 14+:   JSON payload
```

So every message is a 14-byte binary header followed by a JSON body. We implement
`send_message` and `recv_message` to handle this framing.

### Two connections

We open two separate connections to Sway:

- **query_fd_**: for request/response pairs (send GET_TREE, receive the tree)
- **event_fd_**: for subscribed events (window focus changes pushed by Sway)

Why two? Because event messages arrive asynchronously. If we used one connection,
a window focus event could arrive in the middle of reading a GET_TREE response,
corrupting the parse. Separate connections keep the streams independent.

### Finding the focused window

`get_focused_window()` sends a `GET_TREE` message (type 4), which returns a JSON
tree of all windows. We recursively search for the one with `"focused": true`:

```cpp
WindowInfo SwayIpc::find_focused(const nlohmann::json& node) {
    if (node.value("focused", false)) {
        return WindowInfo{
            .app_id = node.value("app_id", ""),
            .title = node.value("name", ""),
            .pid = node.value("pid", 0),
        };
    }

    for (auto& child : node["nodes"]) {
        auto info = find_focused(child);
        if (!info.empty()) return info;
    }
    // also check floating_nodes...
}
```

The tree structure mirrors the layout: outputs contain workspaces, workspaces
contain containers, containers contain windows. We recurse through all of them.

### Event subscription

After subscribing with `MSG_SUBSCRIBE` and payload `["window"]`, Sway pushes
events whenever any window event occurs. We filter for `"change": "focus"` events
and update our cached `focused_window_`.

---

## Agent Detection: Walking /proc

**Files**: `sway/agent_detector.hpp`, `sway/agent_detector.cpp`

When you record a voice note while using Claude Code inside kitty terminal, we want
the history to say "claude code on kitty", not just "kitty." To do this, we walk
the process tree starting from the terminal's PID.

### How Linux tracks processes

Linux exposes process information through the `/proc` virtual filesystem:

- `/proc/1234/comm` — the process name (e.g., "claude", "bash", "node")
- `/proc/1234/cwd` — symlink to the process's current working directory
- `/proc/1234/task/*/children` — space-separated child PIDs

### The tree walk

```
kitty (PID 1000)
  └── zsh (PID 1001)
       └── claude (PID 1002)
            ├── node (PID 1003)
            └── node (PID 1004)
```

Starting from PID 1000 (kitty, which Sway gave us), we:

1. Read `/proc/1000/task/*/children` → find PID 1001
2. Read `/proc/1001/comm` → "zsh" (not an agent, recurse deeper)
3. Read `/proc/1001/task/*/children` → find PID 1002
4. Read `/proc/1002/comm` → "claude" (match!)
5. Read `/proc/1002/cwd` → "/home/user/project"

The search is recursive depth-first. We check every child process's `comm` against
the configured agent list (`["claude", "aider", "gh", "cursor"]`). We use substring
matching (`comm.find(agent)`) because process names might include prefixes.

### Why task/*/children instead of just /proc/*/children?

A multi-threaded process has multiple "tasks" (kernel threads), and each can have
its own children. `/proc/PID/task/TID/children` lists children spawned by that
specific thread. Reading all of them gives us the complete child list.

---

## Output Methods: Getting Text to the User

**Files**: `output/output.hpp`, `output/clipboard_output.hpp/.cpp`, `output/type_output.hpp/.cpp`

### ClipboardOutput: wl-copy

The simplest output: pipe the transcript text to `wl-copy`, which puts it on
the Wayland clipboard.

```cpp
int pipefd[2];
pipe(pipefd);          // create a pipe

pid_t pid = fork();
if (pid == 0) {
    // Child process:
    close(pipefd[1]);                 // close write end
    dup2(pipefd[0], STDIN_FILENO);    // redirect stdin to pipe read end
    close(pipefd[0]);
    execlp("wl-copy", "wl-copy", nullptr);  // replace process with wl-copy
    _exit(127);                        // only reached if exec fails
}

// Parent process:
close(pipefd[0]);                      // close read end
write(pipefd[1], text.data(), text.size());  // write text to pipe
close(pipefd[1]);                      // close write end (signals EOF to child)
waitpid(pid, &status, 0);             // wait for wl-copy to finish
```

This is the standard Unix pattern: **pipe** → **fork** → **dup2** → **exec**.

- `pipe()` creates two file descriptors: one for reading, one for writing.
- `fork()` creates a child process (an exact copy of the parent).
- In the child, `dup2()` redirects stdin to the pipe's read end, then `execlp()`
  replaces the child process with `wl-copy`. The `wl-copy` program reads from
  stdin and puts that text on the clipboard.
- In the parent, we write the transcript to the pipe, close it (sending EOF),
  and wait for the child to finish.

Why fork+exec instead of `system()` or `popen()`? We don't need a shell, and
this avoids shell injection vulnerabilities. The text goes through a pipe, not
through a command line where special characters could be interpreted.

### TypeOutput: wtype

For GUI apps, we can type the text directly using `wtype`, a Wayland virtual
keyboard tool. But for terminal emulators, character-by-character typing is
unreliable (terminals interpret control sequences), so we use a **hybrid approach**:

1. Copy the text to clipboard (using `ClipboardOutput`)
2. Simulate Ctrl+Shift+V (the standard terminal paste shortcut) using `wtype`:
   ```
   wtype -M ctrl -M shift -k v
   ```
   `-M ctrl` means "hold Ctrl", `-M shift` means "hold Shift", `-k v` means "press v".

The event loop decides which approach to use based on whether the focused window
is a known terminal emulator (kitty, alacritty, foot, wezterm).

---

## SQLite History

**Files**: `storage/history_db.hpp`, `storage/history_db.cpp`

### Prepared statements

Instead of building SQL strings with string concatenation (which invites SQL injection),
we use **prepared statements** with parameter binding:

```cpp
const char* insert_sql =
    "INSERT INTO transcriptions (text, audio_duration, ...) VALUES (?, ?, ...)";
sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt_, nullptr);

// Later, to insert a row:
sqlite3_reset(insert_stmt_);                             // reuse the statement
sqlite3_bind_text(insert_stmt_, 1, text.c_str(), -1, SQLITE_TRANSIENT);
sqlite3_bind_double(insert_stmt_, 2, audio_duration);
sqlite3_step(insert_stmt_);                              // execute
```

The `?` placeholders get filled in by `sqlite3_bind_*` calls. This is safe against
injection and also faster — SQLite parses the SQL once and reuses the compiled query.

`SQLITE_TRANSIENT` tells SQLite to make its own copy of the string data. The alternative,
`SQLITE_STATIC`, means "this pointer will stay valid until the statement is finalized" —
which is risky if the string is a local variable.

### WAL mode

```cpp
sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
```

WAL (Write-Ahead Logging) is a SQLite journaling mode that allows concurrent reads
and writes. In the default "delete" mode, a writer blocks all readers. In WAL mode,
readers see a consistent snapshot while a write is in progress. This matters less for
our single-threaded usage, but it's a good default.

### Nullable columns

```cpp
auto bind_nullable = [this](int idx, const std::string& val) {
    if (val.empty()) sqlite3_bind_null(insert_stmt_, idx);
    else sqlite3_bind_text(insert_stmt_, idx, val.c_str(), -1, SQLITE_TRANSIENT);
};
```

Some context fields (like `agent` or `working_dir`) may not be available. Rather
than storing empty strings, we store SQL NULL, which has better semantic meaning
("not detected" vs. "detected but empty").

---

## Configuration

**Files**: `config.hpp`, `config.cpp`

The config system follows the **defaults with overrides** pattern: every field
has a sensible default in the struct definition, and the JSON file only needs
to specify values you want to change.

```cpp
struct Config {
    struct Backend {
        std::string type = "lan";
        std::string url = "http://localhost:8080";
        // ...
    } backend;
    // ...
};
```

The loader checks for each key individually:

```cpp
if (j.contains("backend")) {
    auto& b = j["backend"];
    if (b.contains("type")) cfg.backend.type = b["type"].get<std::string>();
    // ...
}
```

This means a minimal config file like `{"backend": {"url": "http://192.168.1.5:8080"}}`
is valid — everything else uses defaults.

The config file location follows the **XDG Base Directory** spec: `$XDG_CONFIG_HOME/speak-anywhere/config.json`, falling back to `~/.config/speak-anywhere/config.json`.

---

## Daemon Startup and Daemonization

**File**: `src/daemon/main.cpp`

### The double-fork daemon pattern

When running as a background service (without `--foreground`), the daemon
**daemonizes** using the classic Unix double-fork:

```cpp
static void daemonize() {
    pid_t pid = fork();
    if (pid > 0) _exit(0);    // parent exits

    setsid();                  // become session leader (detach from terminal)

    pid = fork();
    if (pid > 0) _exit(0);    // first child exits

    // second child continues as the daemon
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}
```

Why **two** forks?

1. **First fork**: the parent exits, so the shell thinks the command finished.
   The child continues running. `setsid()` makes it a session leader (detached
   from any terminal).
2. **Second fork**: a session leader can accidentally acquire a controlling terminal
   by opening a tty device. The second fork creates a process that is *not* a session
   leader, so this can't happen.

After the double fork, we redirect stdin/stdout/stderr to `/dev/null` since there's
no terminal to write to. (In `--foreground` mode, we skip all of this.)

Note: when using systemd (the `speak-anywhere.service` file), daemonization is
unnecessary because systemd manages the process lifecycle. The service file uses
`Type=simple` and runs with `--foreground`.

---

## Thread Safety Summary

Understanding which thread accesses which data is crucial for reasoning about correctness.

### Main thread (epoll loop)
- Owns: `EventLoop`, `Session`, `IpcServer`, `SwayIpc`, `AgentDetector`, `HistoryDb`
- Reads: `RingBuffer` (via `drain_all()`), `worker_result_`
- Writes: `running_`, `focused_window_`

### PipeWire thread (audio callback)
- Reads: `capturing_` (atomic)
- Writes: `RingBuffer` (via `write()`)
- Accesses nothing else

### Worker thread (transcription)
- Reads: `backend_` (immutable after init), audio data (moved in), `worker_event_fd_`
- Writes: `worker_result_`, `worker_event_fd_`

### Shared data and protection mechanisms

| Data | Writer | Reader | Protection |
|------|--------|--------|------------|
| `RingBuffer` | PipeWire thread | Main thread | Lock-free SPSC (atomics) |
| `capturing_` | Main thread | PipeWire thread | `std::atomic<bool>` |
| `worker_result_` | Worker thread | Main thread | Temporal: written before eventfd, read after eventfd + join |
| `running_` | Main thread | Main thread | `std::atomic<bool>` (for signal safety) |
| `worker_event_fd_` | Worker thread | Main thread (epoll) | Kernel eventfd (thread-safe by design) |

The key insight: there are **no mutexes** in this codebase. Thread safety comes from:

1. **Lock-free atomics** (ring buffer, flags)
2. **Temporal ordering** (worker writes result, then signals eventfd; main thread
   reads eventfd, then joins worker, then reads result — the join provides a
   happens-before guarantee)
3. **Kernel primitives** (eventfd, signalfd are inherently thread-safe)
4. **Isolation** (most data is only accessed by one thread)
