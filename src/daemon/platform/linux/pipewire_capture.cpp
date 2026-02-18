#include "platform/linux/pipewire_capture.hpp"

#include <print>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

PipeWireCapture::PipeWireCapture(RingBuffer& ring_buf, uint32_t sample_rate)
    : ring_buf_(ring_buf), sample_rate_(sample_rate) {
    pw_init(nullptr, nullptr);
}

PipeWireCapture::~PipeWireCapture() {
    stop();
    pw_deinit();
}

bool PipeWireCapture::start() {
    if (capturing_.load(std::memory_order_relaxed)) return true;

    loop_ = pw_thread_loop_new("speak-anywhere", nullptr);
    if (!loop_) {
        std::println(stderr, "audio: failed to create thread loop");
        return false;
    }

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Communication",
        PW_KEY_NODE_NAME, "speak-anywhere",
        PW_KEY_APP_NAME, "speak-anywhere",
        nullptr
    );

    stream_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_),
        "speak-anywhere-capture",
        props,
        &stream_events_,
        this
    );

    if (!stream_) {
        std::println(stderr, "audio: failed to create stream");
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    // Build format params: S16_LE, mono, 16kHz
    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    auto info = SPA_AUDIO_INFO_RAW_INIT(
        .format = SPA_AUDIO_FORMAT_S16_LE,
        .rate = sample_rate_,
        .channels = 1
    );
    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    int ret = pw_stream_connect(
        stream_,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS
        ),
        params, 1
    );

    if (ret < 0) {
        std::println(stderr, "audio: stream connect failed: {}", spa_strerror(ret));
        pw_stream_destroy(stream_);
        stream_ = nullptr;
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    ret = pw_thread_loop_start(loop_);
    if (ret < 0) {
        std::println(stderr, "audio: thread loop start failed: {}", spa_strerror(ret));
        pw_stream_destroy(stream_);
        stream_ = nullptr;
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
        return false;
    }

    ring_buf_.reset();
    capturing_.store(true, std::memory_order_release);
    return true;
}

void PipeWireCapture::stop() {
    if (!capturing_.load(std::memory_order_relaxed)) return;

    capturing_.store(false, std::memory_order_release);

    if (loop_) {
        pw_thread_loop_stop(loop_);
    }
    if (stream_) {
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }
    if (loop_) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
}

void PipeWireCapture::on_process(void* userdata) {
    auto* self = static_cast<PipeWireCapture*>(userdata);

    auto* buf = pw_stream_dequeue_buffer(self->stream_);
    if (!buf) return;

    auto* d = &buf->buffer->datas[0];
    if (!d->data) {
        pw_stream_queue_buffer(self->stream_, buf);
        return;
    }

    auto* data = static_cast<const uint8_t*>(d->data) + d->chunk->offset;
    size_t size = d->chunk->size;

    if (self->capturing_.load(std::memory_order_relaxed)) {
        self->ring_buf_.write(data, size);
    }

    pw_stream_queue_buffer(self->stream_, buf);
}

void PipeWireCapture::on_state_changed(void* /*userdata*/, enum pw_stream_state old,
                                       enum pw_stream_state state, const char* error) {
    if (error) {
        std::println(stderr, "audio: stream state {} -> {}: {}",
                     pw_stream_state_as_string(old),
                     pw_stream_state_as_string(state),
                     error);
    }
}
