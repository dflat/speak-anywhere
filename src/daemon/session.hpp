#pragma once

#include "audio_capture.hpp"
#include "ring_buffer.hpp"
#include "sway/window_info.hpp"
#include "whisper/backend.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class SessionState { Idle, Recording, Transcribing };

class Session {
public:
    using TranscribeCallback = std::function<void(std::vector<int16_t> audio,
                                                  uint32_t sample_rate,
                                                  WindowInfo context)>;

    Session(RingBuffer& ring_buf, AudioCapture& capture, uint32_t sample_rate);

    bool start_recording(const WindowInfo& window);
    // Returns audio samples if recording was active, empty if not.
    std::vector<int16_t> stop_recording();
    void set_transcribing();
    void set_idle();

    SessionState state() const { return state_; }
    double recording_duration() const;
    const WindowInfo& window_context() const { return window_context_; }

private:
    RingBuffer& ring_buf_;
    AudioCapture& capture_;
    uint32_t sample_rate_;
    SessionState state_ = SessionState::Idle;
    std::chrono::steady_clock::time_point record_start_;
    WindowInfo window_context_;
};
