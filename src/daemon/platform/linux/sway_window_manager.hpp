#pragma once

#include "platform/window_manager.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

class SwayWindowManager : public WindowManager {
public:
    SwayWindowManager();
    ~SwayWindowManager() override;

    SwayWindowManager(const SwayWindowManager&) = delete;
    SwayWindowManager& operator=(const SwayWindowManager&) = delete;

    bool connect() override;
    bool subscribe_focus_events() override;
    WindowInfo get_focused_window() override;
    int event_fd() const override { return event_fd_; }
    bool read_event(WindowInfo& info) override;

private:
    static constexpr char MAGIC[] = "i3-ipc";
    static constexpr uint32_t MSG_GET_TREE = 4;
    static constexpr uint32_t MSG_SUBSCRIBE = 2;
    static constexpr uint32_t EVENT_WINDOW = 0x80000003;

    bool send_message(int fd, uint32_t type, const std::string& payload = "");
    bool recv_message(int fd, uint32_t& type, std::string& payload);
    int connect_socket(const std::string& path);
    static WindowInfo find_focused(const nlohmann::json& node);

    int query_fd_ = -1;
    int event_fd_ = -1;
    std::string sway_sock_;
};
