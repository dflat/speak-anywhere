#pragma once

#include "ring_buffer.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

class AudioCapture {
public:
    explicit AudioCapture(RingBuffer& ring_buf, uint32_t sample_rate = 16000);
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    bool start();
    void stop();
    bool is_capturing() const { return capturing_.load(std::memory_order_relaxed); }

private:
    static void on_process(void* userdata);
    static void on_state_changed(void* userdata, enum pw_stream_state old,
                                 enum pw_stream_state state, const char* error);

    RingBuffer& ring_buf_;
    uint32_t sample_rate_;
    std::atomic<bool> capturing_{false};

    pw_thread_loop* loop_ = nullptr;
    pw_stream* stream_ = nullptr;

    static constexpr pw_stream_events stream_events_ = {
        .version = PW_VERSION_STREAM_EVENTS,
        .state_changed = on_state_changed,
        .process = on_process,
    };
};
