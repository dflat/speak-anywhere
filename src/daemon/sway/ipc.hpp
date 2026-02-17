#pragma once

#include "window_info.hpp"

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

class SwayIpc {
public:
    SwayIpc();
    ~SwayIpc();

    SwayIpc(const SwayIpc&) = delete;
    SwayIpc& operator=(const SwayIpc&) = delete;

    // Connect to Sway IPC. Returns false if $SWAYSOCK not set or connection fails.
    bool connect();

    // Subscribe to window focus events. Must call connect() first.
    bool subscribe_window_events();

    // Get the currently focused window info.
    WindowInfo get_focused_window();

    // Read and parse a Sway event. Call when event_fd() is readable.
    // Returns true if a window focus event was read, updates `info`.
    bool read_event(WindowInfo& info);

    // FD for epoll registration (event subscription socket).
    int event_fd() const { return event_fd_; }

private:
    // i3-ipc binary protocol
    static constexpr char MAGIC[] = "i3-ipc";
    static constexpr uint32_t MSG_RUN_COMMAND = 0;
    static constexpr uint32_t MSG_GET_TREE = 4;
    static constexpr uint32_t MSG_SUBSCRIBE = 2;
    static constexpr uint32_t EVENT_WINDOW = 0x80000003;

    bool send_message(int fd, uint32_t type, const std::string& payload = "");
    bool recv_message(int fd, uint32_t& type, std::string& payload);

    int connect_socket(const std::string& path);

    static WindowInfo find_focused(const nlohmann::json& node);

    int query_fd_ = -1;   // for GET_TREE etc.
    int event_fd_ = -1;   // for subscribed events
    std::string sway_sock_;
};
