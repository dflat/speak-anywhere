#include "platform/linux/linux_event_loop.hpp"

#include "platform/linux/wayland_clipboard_output.hpp"
#include "platform/linux/wayland_type_output.hpp"
#include "platform/platform_paths.hpp"

#include <cerrno>
#include <cstring>
#include <print>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

LinuxEventLoop::LinuxEventLoop(Config config, bool verbose)
    : config_(std::move(config)), verbose_(verbose),
      ring_buf_(config_.audio.ring_buffer_bytes()),
      audio_capture_(ring_buf_, config_.audio.sample_rate),
      detector_(config_.agents),
      core_(config_, verbose_, ring_buf_, audio_capture_,
            detector_, ipc_server_,
            // OutputFactory
            [](const std::string& method, bool is_terminal) -> std::unique_ptr<OutputMethod> {
                if (method == "type") return std::make_unique<WaylandTypeOutput>(is_terminal);
                return std::make_unique<WaylandClipboardOutput>();
            },
            // NotifyCallback
            [this]() {
                uint64_t val = 1;
                ::write(worker_event_fd_, &val, sizeof(val));
            }) {}

LinuxEventLoop::~LinuxEventLoop() {
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
    if (signal_fd_ >= 0) ::close(signal_fd_);
    if (worker_event_fd_ >= 0) ::close(worker_event_fd_);
}

bool LinuxEventLoop::init() {
    // IPC socket
    auto ipc_path = platform::ipc_endpoint();
    if (!ipc_server_.start(ipc_path)) return false;
    log("IPC listening on " + ipc_path);

    // Window manager (optional)
    if (window_mgr_.connect()) {
        core_.set_focused_window(window_mgr_.get_focused_window());
        if (window_mgr_.subscribe_focus_events()) {
            log("Sway IPC connected");
        }
    } else {
        log("Sway IPC not available (window context disabled)");
    }

    // Core init (backend, history db)
    if (!core_.init()) return false;

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

    if (window_mgr_.event_fd() >= 0) {
        add_fd(window_mgr_.event_fd(), EPOLLIN);
    }

    running_.store(true, std::memory_order_release);
    return true;
}

void LinuxEventLoop::run() {
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
                signalfd_siginfo info;
                ::read(signal_fd_, &info, sizeof(info));
                log("Received signal, shutting down");
                running_.store(false, std::memory_order_release);
                break;
            }

            if (fd == ipc_server_.server_fd()) {
                int client_fd = ipc_server_.accept_client();
                if (client_fd >= 0) {
                    epoll_event ev{.events = EPOLLIN, .data = {.fd = client_fd}};
                    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev);
                }
                continue;
            }

            if (fd == worker_event_fd_) {
                uint64_t val;
                ::read(worker_event_fd_, &val, sizeof(val));
                core_.on_transcription_complete();
                continue;
            }

            if (fd == window_mgr_.event_fd()) {
                WindowInfo info;
                if (window_mgr_.read_event(info)) {
                    core_.set_focused_window(info);
                }
                continue;
            }

            // Client fd
            nlohmann::json cmd;
            if (ipc_server_.read_command(fd, cmd)) {
                std::string cmd_str = cmd.value("cmd", "");
                auto response = core_.handle_command(cmd_str, cmd);

                if (response.value("status", "") == "transcribing") {
                    core_.add_waiting_client(fd);
                } else {
                    ipc_server_.send_response(fd, response);
                }
            } else {
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                ipc_server_.close_client(fd);
                core_.remove_waiting_client(fd);
            }
        }
    }

    // Clean shutdown
    core_.shutdown();
}

void LinuxEventLoop::request_stop() {
    running_.store(false, std::memory_order_release);
}

void LinuxEventLoop::log(const std::string& msg) {
    if (verbose_) {
        std::println(stderr, "[speak-anywhere] {}", msg);
    }
}
