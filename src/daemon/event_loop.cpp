#include "event_loop.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <print>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace fs = std::filesystem;

EventLoop::EventLoop(Config config, bool verbose)
    : config_(std::move(config)), verbose_(verbose),
      ring_buf_(config_.audio.ring_buffer_bytes),
      audio_capture_(ring_buf_, config_.audio.sample_rate),
      session_(ring_buf_, audio_capture_, config_.audio.sample_rate),
      agent_detector_(config_.agents) {}

EventLoop::~EventLoop() {
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
    if (signal_fd_ >= 0) ::close(signal_fd_);
    if (worker_event_fd_ >= 0) ::close(worker_event_fd_);
}

bool EventLoop::init() {
    // Create backend
    if (config_.backend.type == "lan") {
        backend_ = std::make_unique<LanBackend>(
            config_.backend.url, config_.backend.api_format, config_.backend.language);
    } else {
        std::println(stderr, "Unknown backend type: {}", config_.backend.type);
        return false;
    }

    // Open history DB
    const char* xdg_data = std::getenv("XDG_DATA_HOME");
    std::string db_path;
    if (xdg_data) {
        db_path = std::string(xdg_data) + "/speak-anywhere/history.db";
    } else {
        const char* home = std::getenv("HOME");
        db_path = std::string(home ? home : "/tmp") + "/.local/share/speak-anywhere/history.db";
    }
    if (!history_db_.open(db_path)) {
        std::println(stderr, "Warning: history DB failed to open, history disabled");
    }

    // IPC socket
    const char* xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
    std::string ipc_path;
    if (xdg_runtime) {
        ipc_path = std::string(xdg_runtime) + "/speak-anywhere.sock";
    } else {
        ipc_path = "/tmp/speak-anywhere.sock";
    }
    if (!ipc_server_.start(ipc_path)) return false;
    log("IPC listening on " + ipc_path);

    // Sway IPC (optional — don't fail if not available)
    if (sway_ipc_.connect()) {
        focused_window_ = sway_ipc_.get_focused_window();
        if (sway_ipc_.subscribe_window_events()) {
            log("Sway IPC connected");
        }
    } else {
        log("Sway IPC not available (window context disabled)");
    }

    // epoll setup
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        std::println(stderr, "epoll_create1 failed: {}", std::strerror(errno));
        return false;
    }

    // Signal handling via signalfd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ < 0) {
        std::println(stderr, "signalfd failed: {}", std::strerror(errno));
        return false;
    }

    // Worker notification eventfd
    worker_event_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (worker_event_fd_ < 0) {
        std::println(stderr, "eventfd failed: {}", std::strerror(errno));
        return false;
    }

    // Register FDs with epoll
    auto add_fd = [this](int fd, uint32_t events) {
        epoll_event ev{.events = events, .data = {.fd = fd}};
        return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
    };

    add_fd(signal_fd_, EPOLLIN);
    add_fd(ipc_server_.server_fd(), EPOLLIN);
    add_fd(worker_event_fd_, EPOLLIN);

    if (sway_ipc_.event_fd() >= 0) {
        add_fd(sway_ipc_.event_fd(), EPOLLIN);
    }

    running_.store(true, std::memory_order_release);
    return true;
}

void EventLoop::run() {
    constexpr int MAX_EVENTS = 16;
    epoll_event events[MAX_EVENTS];

    while (running_.load(std::memory_order_relaxed)) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::println(stderr, "epoll_wait error: {}", std::strerror(errno));
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == signal_fd_) {
                // Signal received — shut down
                signalfd_siginfo info;
                ::read(signal_fd_, &info, sizeof(info));
                log("Received signal, shutting down");
                running_.store(false, std::memory_order_release);
                break;
            }

            if (fd == ipc_server_.server_fd()) {
                // New client connection
                int client_fd = ipc_server_.accept_client();
                if (client_fd >= 0) {
                    epoll_event ev{.events = EPOLLIN, .data = {.fd = client_fd}};
                    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev);
                }
                continue;
            }

            if (fd == worker_event_fd_) {
                // Worker thread completed
                uint64_t val;
                ::read(worker_event_fd_, &val, sizeof(val));
                on_transcription_complete();
                continue;
            }

            if (fd == sway_ipc_.event_fd()) {
                // Sway window event
                WindowInfo info;
                if (sway_ipc_.read_event(info)) {
                    focused_window_ = info;
                }
                continue;
            }

            // Must be a client fd
            nlohmann::json cmd;
            if (ipc_server_.read_command(fd, cmd)) {
                std::string cmd_str = cmd.value("cmd", "");
                nlohmann::json response;

                if (cmd_str == "start") response = handle_start(cmd);
                else if (cmd_str == "stop") response = handle_stop(cmd);
                else if (cmd_str == "toggle") response = handle_toggle(cmd);
                else if (cmd_str == "status") response = handle_status(cmd);
                else if (cmd_str == "history") response = handle_history(cmd);
                else response = {{"status", "error"}, {"message", "unknown command"}};

                // If stop returns "transcribing", we defer the response
                if (response.value("status", "") == "transcribing") {
                    waiting_clients_.push_back(fd);
                } else {
                    ipc_server_.send_response(fd, response);
                }
            } else {
                // Client disconnected or error
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                ipc_server_.close_client(fd);
                std::erase(waiting_clients_, fd);
            }
        }
    }

    // Clean shutdown
    if (session_.state() == SessionState::Recording) {
        audio_capture_.stop();
    }
    
    if (session_.state() == SessionState::Transcribing) {
        log("Waiting for pending transcription to complete...");
        on_transcription_complete();
    } else if (worker_.joinable()) {
        worker_.join();
    }
}

void EventLoop::request_stop() {
    running_.store(false, std::memory_order_release);
}

nlohmann::json EventLoop::handle_start(const nlohmann::json& cmd) {
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

nlohmann::json EventLoop::handle_stop(const nlohmann::json& /*cmd*/) {
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

nlohmann::json EventLoop::handle_toggle(const nlohmann::json& cmd) {
    if (session_.state() == SessionState::Recording) {
        return handle_stop(cmd);
    }
    return handle_start(cmd);
}

nlohmann::json EventLoop::handle_status(const nlohmann::json& /*cmd*/) {
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

nlohmann::json EventLoop::handle_history(const nlohmann::json& cmd) {
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

void EventLoop::start_transcription(std::vector<int16_t> audio, WindowInfo context,
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

        // Notify main thread
        uint64_t val = 1;
        ::write(worker_event_fd_, &val, sizeof(val));
    });
}

void EventLoop::on_transcription_complete() {
    if (worker_.joinable()) {
        worker_.join();
    }

    auto& wr = worker_result_;
    nlohmann::json response;

    if (wr.result.has_value()) {
        auto& tr = wr.result.value();
        log(std::format("Transcription complete: {:.1f}s processing, {} chars",
                        tr.processing_s, tr.text.size()));

        // Deliver output
        std::string app = !wr.context.app_id.empty() ? wr.context.app_id : wr.context.window_class;
        std::transform(app.begin(), app.end(), app.begin(), ::tolower);

        bool is_terminal = !app.empty() &&
            (app.find("kitty") != std::string::npos ||
             app.find("alacritty") != std::string::npos ||
             app.find("foot") != std::string::npos ||
             app.find("wezterm") != std::string::npos);

        auto output = make_output(wr.output_method, is_terminal);
        if (output && !tr.text.empty()) {
            auto res = output->deliver(tr.text);
            if (!res) {
                log("Output delivery failed: " + res.error());
            }
        }

        // Store in history
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

    // Send to waiting clients
    for (int fd : waiting_clients_) {
        ipc_server_.send_response(fd, response);
    }
    waiting_clients_.clear();

    session_.set_idle();
}

std::unique_ptr<OutputMethod> EventLoop::make_output(const std::string& method, bool is_terminal) {
    if (method == "type") {
        return std::make_unique<TypeOutput>(is_terminal);
    }
    return std::make_unique<ClipboardOutput>();
}

WindowInfo EventLoop::enrich_window_info(WindowInfo info) {
    if (info.pid > 0) {
        auto detection = agent_detector_.detect(info.pid);
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

void EventLoop::log(const std::string& msg) {
    if (verbose_) {
        std::println(stderr, "[speak-anywhere] {}", msg);
    }
}
