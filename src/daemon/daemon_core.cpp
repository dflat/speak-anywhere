#include "daemon_core.hpp"

#include "platform/platform_paths.hpp"
#include "whisper/lan_backend.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <print>

DaemonCore::DaemonCore(Config config, bool verbose,
                       RingBuffer& ring_buf, AudioCapture& audio,
                       ProcessDetector& detector, IpcServer& ipc,
                       OutputFactory output_factory, NotifyCallback notify)
    : config_(std::move(config)), verbose_(verbose),
      ring_buf_(ring_buf), audio_(audio),
      detector_(detector), ipc_(ipc),
      output_factory_(std::move(output_factory)),
      notify_(std::move(notify)),
      session_(ring_buf_, audio_, config_.audio.sample_rate) {}

DaemonCore::~DaemonCore() = default;

bool DaemonCore::init() {
    // Create backend
    if (config_.backend.type == "lan") {
        backend_ = std::make_unique<LanBackend>(
            config_.backend.url, config_.backend.api_format, config_.backend.language);
    } else {
        std::println(stderr, "Unknown backend type: {}", config_.backend.type);
        return false;
    }

    // Open history DB
    auto data = platform::data_dir();
    std::string db_path;
    if (!data.empty()) {
        db_path = data + "/history.db";
    } else {
        db_path = "/tmp/speak-anywhere/history.db";
    }
    if (!history_db_.open(db_path)) {
        std::println(stderr, "Warning: history DB failed to open, history disabled");
    }

    return true;
}

nlohmann::json DaemonCore::handle_command(const std::string& cmd_str,
                                          const nlohmann::json& cmd) {
    if (cmd_str == "start") return handle_start(cmd);
    if (cmd_str == "stop") return handle_stop(cmd);
    if (cmd_str == "toggle") return handle_toggle(cmd);
    if (cmd_str == "status") return handle_status(cmd);
    if (cmd_str == "history") return handle_history(cmd);
    return {{"status", "error"}, {"message", "unknown command"}};
}

nlohmann::json DaemonCore::handle_start(const nlohmann::json& cmd) {
    if (session_.state() != SessionState::Idle) {
        return {{"status", "error"}, {"message", "already recording or transcribing"}};
    }

    pending_output_method_ = cmd.value("output", config_.output.default_method);

    auto window = enrich_window_info(focused_window_);
    if (!session_.start_recording(window)) {
        return {{"status", "error"}, {"message", "failed to start recording"}};
    }

    log("Recording started" + (window.context.empty() ? "" : " (" + window.context + ")"));
    return {{"status", "ok"}, {"message", "recording"}};
}

nlohmann::json DaemonCore::handle_stop(const nlohmann::json& /*cmd*/) {
    if (session_.state() != SessionState::Recording) {
        return {{"status", "error"}, {"message", "not recording"}};
    }

    auto audio = session_.stop_recording();
    if (audio.empty()) {
        session_.set_idle();
        return {{"status", "error"}, {"message", "no audio captured"}};
    }

    double duration = static_cast<double>(audio.size()) / config_.audio.sample_rate;
    log(std::format("Recording stopped, {:.1f}s audio, transcribing...", duration));

    start_transcription(std::move(audio), session_.window_context(), pending_output_method_);

    return {{"status", "transcribing"}, {"duration", duration}};
}

nlohmann::json DaemonCore::handle_toggle(const nlohmann::json& cmd) {
    if (session_.state() == SessionState::Recording) {
        return handle_stop(cmd);
    }
    return handle_start(cmd);
}

nlohmann::json DaemonCore::handle_status(const nlohmann::json& /*cmd*/) {
    nlohmann::json resp = {{"status", "ok"}};
    switch (session_.state()) {
        case SessionState::Idle:
            resp["state"] = "idle";
            break;
        case SessionState::Recording:
            resp["state"] = "recording";
            resp["duration"] = session_.recording_duration();
            break;
        case SessionState::Transcribing:
            resp["state"] = "transcribing";
            break;
    }
    return resp;
}

nlohmann::json DaemonCore::handle_history(const nlohmann::json& cmd) {
    int limit = cmd.value("limit", 10);
    auto entries = history_db_.recent(limit);

    nlohmann::json resp = {{"status", "ok"}, {"entries", nlohmann::json::array()}};
    for (auto& e : entries) {
        resp["entries"].push_back({
            {"id", e.id},
            {"timestamp", e.timestamp},
            {"text", e.text},
            {"audio_duration", e.audio_duration},
            {"processing_time", e.processing_time},
            {"app_context", e.app_context},
        });
    }
    return resp;
}

void DaemonCore::start_transcription(std::vector<int16_t> audio, WindowInfo context,
                                     const std::string& output_method) {
    worker_result_ = {};

    worker_ = std::jthread([this, audio = std::move(audio), context = std::move(context),
                            output_method, sample_rate = config_.audio.sample_rate]
                           (std::stop_token) mutable {
        auto result = backend_->transcribe(audio, sample_rate);
        worker_result_ = WorkerResult{
            .result = std::move(result),
            .context = std::move(context),
            .output_method = output_method,
        };

        notify_();
    });
}

void DaemonCore::on_transcription_complete() {
    if (worker_.joinable()) {
        worker_.join();
    }

    auto& wr = worker_result_;
    nlohmann::json response;

    if (wr.result.has_value()) {
        auto& tr = wr.result.value();
        log(std::format("Transcription complete: {:.1f}s processing, {} chars",
                        tr.processing_s, tr.text.size()));

        // Determine if focused app is a terminal
        std::string app = !wr.context.app_id.empty() ? wr.context.app_id : wr.context.window_class;
        std::transform(app.begin(), app.end(), app.begin(), ::tolower);

        bool is_terminal = !app.empty() &&
            (app.find("kitty") != std::string::npos ||
             app.find("alacritty") != std::string::npos ||
             app.find("foot") != std::string::npos ||
             app.find("wezterm") != std::string::npos);

        auto output = output_factory_(wr.output_method, is_terminal);
        if (output && !tr.text.empty()) {
            auto res = output->deliver(tr.text);
            if (!res) {
                log("Output delivery failed: " + res.error());
            }
        }

        history_db_.insert(tr.text, tr.duration_s, tr.processing_s, wr.context, config_.backend.type);

        response = {
            {"status", "ok"},
            {"text", tr.text},
            {"duration", tr.duration_s},
            {"processing_time", tr.processing_s},
        };
    } else {
        log("Transcription failed: " + wr.result.error());
        response = {{"status", "error"}, {"message", wr.result.error()}};
    }

    for (int fd : waiting_clients_) {
        ipc_.send_response(fd, response);
    }
    waiting_clients_.clear();

    session_.set_idle();
}

void DaemonCore::add_waiting_client(int fd) {
    waiting_clients_.push_back(fd);
}

void DaemonCore::remove_waiting_client(int fd) {
    std::erase(waiting_clients_, fd);
}

void DaemonCore::set_focused_window(const WindowInfo& info) {
    focused_window_ = info;
}

void DaemonCore::shutdown() {
    if (session_.state() == SessionState::Recording) {
        audio_.stop();
    }

    if (session_.state() == SessionState::Transcribing) {
        log("Waiting for pending transcription to complete...");
        on_transcription_complete();
    } else if (worker_.joinable()) {
        worker_.join();
    }
}

WindowInfo DaemonCore::enrich_window_info(WindowInfo info) {
    if (info.pid > 0) {
        auto detection = detector_.detect(info.pid);
        std::string app = !info.app_id.empty() ? info.app_id : info.window_class;
        if (!detection.agent.empty()) {
            info.agent = detection.agent;
            info.working_dir = detection.working_dir;
            info.context = detection.agent + " code on " + app;
        } else {
            info.context = app;
        }
    }
    return info;
}

void DaemonCore::log(const std::string& msg) {
    if (verbose_) {
        std::println(stderr, "[speak-anywhere] {}", msg);
    }
}
