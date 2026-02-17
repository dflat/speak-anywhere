#include <catch2/catch_test_macros.hpp>

#include "audio_capture.hpp"
#include "ring_buffer.hpp"
#include "session.hpp"

TEST_CASE("Session state machine", "[session]") {
    RingBuffer ring(1024);
    AudioCapture capture(ring, 16000);
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
