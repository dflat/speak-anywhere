#include <catch2/catch_test_macros.hpp>

#include "platform/audio_capture.hpp"
#include "ring_buffer.hpp"
#include "session.hpp"

class MockAudioCapture : public AudioCapture {
public:
    bool start() override { capturing_ = true; return true; }
    void stop() override { capturing_ = false; }
    bool is_capturing() const override { return capturing_; }
private:
    bool capturing_ = false;
};

TEST_CASE("Session state machine", "[session]") {
    RingBuffer ring(1024);
    MockAudioCapture capture;
    Session session(ring, capture, 16000);

    SECTION("InitialStateIdle") {
        REQUIRE(session.state() == SessionState::Idle);
    }

    SECTION("StopWhenIdleReturnsEmpty") {
        auto samples = session.stop_recording();
        REQUIRE(samples.empty());
    }

    SECTION("SetTranscribingFromIdle") {
        session.set_transcribing();
        REQUIRE(session.state() == SessionState::Transcribing);
    }

    SECTION("SetIdleFromTranscribing") {
        session.set_transcribing();
        REQUIRE(session.state() == SessionState::Transcribing);
        session.set_idle();
        REQUIRE(session.state() == SessionState::Idle);
    }

    SECTION("RecordingDurationZeroWhenIdle") {
        REQUIRE(session.recording_duration() == 0.0);
    }
}
