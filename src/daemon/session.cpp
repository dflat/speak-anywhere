#include "session.hpp"

#include <print>

Session::Session(RingBuffer& ring_buf, AudioCapture& capture, uint32_t sample_rate)
    : ring_buf_(ring_buf), capture_(capture), sample_rate_(sample_rate) {}

bool Session::start_recording(const WindowInfo& window) {
    if (state_ != SessionState::Idle) {
        std::println(stderr, "session: cannot start, state is not idle");
        return false;
    }

    ring_buf_.reset();
    if (!capture_.start()) {
        std::println(stderr, "session: failed to start audio capture");
        return false;
    }

    window_context_ = window;
    record_start_ = std::chrono::steady_clock::now();
    state_ = SessionState::Recording;
    return true;
}

std::vector<int16_t> Session::stop_recording() {
    if (state_ != SessionState::Recording) {
        return {};
    }

    capture_.stop();
    auto samples = ring_buf_.drain_all();
    state_ = SessionState::Transcribing;
    return samples;
}

void Session::set_transcribing() {
    state_ = SessionState::Transcribing;
}

void Session::set_idle() {
    state_ = SessionState::Idle;
}

double Session::recording_duration() const {
    if (state_ != SessionState::Recording) return 0.0;
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - record_start_).count();
}
