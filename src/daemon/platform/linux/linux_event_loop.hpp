#pragma once

#include "config.hpp"
#include "daemon_core.hpp"
#include "platform/linux/pipewire_capture.hpp"
#include "platform/linux/procfs_detector.hpp"
#include "platform/linux/sway_window_manager.hpp"
#include "platform/linux/unix_socket_server.hpp"
#include "ring_buffer.hpp"

#include <atomic>

class LinuxEventLoop {
public:
    explicit LinuxEventLoop(Config config, bool verbose = false);
    ~LinuxEventLoop();

    LinuxEventLoop(const LinuxEventLoop&) = delete;
    LinuxEventLoop& operator=(const LinuxEventLoop&) = delete;

    bool init();
    void run();
    void request_stop();

private:
    void log(const std::string& msg);

    Config config_;
    bool verbose_;

    // Platform implementations (constructed before core_)
    RingBuffer ring_buf_;
    PipeWireCapture audio_capture_;
    SwayWindowManager window_mgr_;
    ProcfsDetector detector_;
    UnixSocketServer ipc_server_;

    // Portable business logic
    DaemonCore core_;

    // Linux event loop
    int epoll_fd_ = -1;
    int signal_fd_ = -1;
    int worker_event_fd_ = -1;

    std::atomic<bool> running_{false};
};
