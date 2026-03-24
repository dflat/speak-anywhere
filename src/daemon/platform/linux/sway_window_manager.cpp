#include "platform/linux/sway_window_manager.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <linux/input.h>
#include <print>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

#ifndef NLONGS
#define NLONGS(x) (((x) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
#endif

SwayWindowManager::SwayWindowManager() = default;

SwayWindowManager::~SwayWindowManager() {
    if (query_fd_ >= 0) ::close(query_fd_);
    if (event_fd_ >= 0) ::close(event_fd_);
}

bool SwayWindowManager::is_modifier_down() {
    // Check all event devices for modifier keys
    try {
        if (!fs::exists("/dev/input")) return false;
        for (auto const& entry : fs::directory_iterator("/dev/input")) {
            if (entry.path().filename().string().starts_with("event")) {
                int fd = ::open(entry.path().c_str(), O_RDONLY | O_NONBLOCK);
                if (fd < 0) continue;

                // Check if it's a keyboard (supports KEY_LEFTCTRL)
                unsigned long key_bits[NLONGS(KEY_CNT)] = {0};
                if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
                    ::close(fd);
                    continue;
                }

                auto test_bit = [&](int bit) {
                    return (key_bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1;
                };

                if (!test_bit(KEY_LEFTCTRL) && !test_bit(KEY_RIGHTCTRL) &&
                    !test_bit(KEY_LEFTALT) && !test_bit(KEY_RIGHTALT) &&
                    !test_bit(KEY_LEFTMETA) && !test_bit(KEY_RIGHTMETA)) {
                    ::close(fd);
                    continue;
                }

                // It is a keyboard, check current state
                unsigned long key_state[NLONGS(KEY_CNT)] = {0};
                if (::ioctl(fd, EVIOCGKEY(sizeof(key_state)), key_state) >= 0) {
                    auto is_down = [&](int bit) {
                        return (key_state[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1;
                    };

                    if (is_down(KEY_LEFTCTRL) || is_down(KEY_RIGHTCTRL) ||
                        is_down(KEY_LEFTALT) || is_down(KEY_RIGHTALT) ||
                        is_down(KEY_LEFTMETA) || is_down(KEY_RIGHTMETA) ||
                        is_down(KEY_LEFTSHIFT) || is_down(KEY_RIGHTSHIFT)) {
                        ::close(fd);
                        return true;
                    }
                }
                ::close(fd);
            }
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool SwayWindowManager::connect() {
    const char* sock = std::getenv("SWAYSOCK");
    if (!sock) {
        std::println(stderr, "sway: $SWAYSOCK not set");
        return false;
    }
    sway_sock_ = sock;

    query_fd_ = connect_socket(sway_sock_);
    if (query_fd_ < 0) return false;

    return true;
}

bool SwayWindowManager::subscribe_focus_events() {
    event_fd_ = connect_socket(sway_sock_);
    if (event_fd_ < 0) return false;

    if (!send_message(event_fd_, MSG_SUBSCRIBE, R"(["window"])")) {
        ::close(event_fd_);
        event_fd_ = -1;
        return false;
    }

    uint32_t type;
    std::string payload;
    if (!recv_message(event_fd_, type, payload)) {
        ::close(event_fd_);
        event_fd_ = -1;
        return false;
    }

    return true;
}

WindowInfo SwayWindowManager::get_focused_window() {
    if (!send_message(query_fd_, MSG_GET_TREE)) return {};

    uint32_t type;
    std::string payload;
    if (!recv_message(query_fd_, type, payload)) return {};

    try {
        auto j = nlohmann::json::parse(payload);
        return find_focused(j);
    } catch (...) {
        return {};
    }
}

bool SwayWindowManager::read_event(WindowInfo& info) {
    if (event_fd_ < 0) return false;

    uint32_t type;
    std::string payload;
    if (!recv_message(event_fd_, type, payload)) return false;

    if (type != EVENT_WINDOW) return false;

    try {
        auto j = nlohmann::json::parse(payload);
        auto change = j.value("change", "");
        if (change != "focus") return false;

        if (j.contains("container")) {
            auto& c = j["container"];
            info.app_id = c.value("app_id", "");
            if (info.app_id.empty() && c.contains("window_properties")) {
                info.window_class = c["window_properties"].value("class", "");
            }
            info.title = c.value("name", "");
            info.pid = c.value("pid", 0);
        }
        return true;
    } catch (...) {
        return false;
    }
}

int SwayWindowManager::connect_socket(const std::string& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::println(stderr, "sway: connect failed: {}", std::strerror(errno));
        ::close(fd);
        return -1;
    }
    return fd;
}

bool SwayWindowManager::send_message(int fd, uint32_t type, const std::string& payload) {
    uint32_t len = static_cast<uint32_t>(payload.size());
    char header[14];
    std::memcpy(header, MAGIC, 6);
    std::memcpy(header + 6, &len, 4);
    std::memcpy(header + 10, &type, 4);

    if (::send(fd, header, 14, MSG_NOSIGNAL) != 14) return false;
    if (len > 0) {
        if (::send(fd, payload.data(), len, MSG_NOSIGNAL) != static_cast<ssize_t>(len))
            return false;
    }
    return true;
}

bool SwayWindowManager::recv_message(int fd, uint32_t& type, std::string& payload) {
    char header[14];
    size_t read_total = 0;
    while (read_total < 14) {
        ssize_t n = ::recv(fd, header + read_total, 14 - read_total, 0);
        if (n <= 0) return false;
        read_total += static_cast<size_t>(n);
    }

    if (std::memcmp(header, MAGIC, 6) != 0) return false;

    uint32_t len;
    std::memcpy(&len, header + 6, 4);
    std::memcpy(&type, header + 10, 4);

    payload.resize(len);
    read_total = 0;
    while (read_total < len) {
        ssize_t n = ::recv(fd, payload.data() + read_total, len - read_total, 0);
        if (n <= 0) return false;
        read_total += static_cast<size_t>(n);
    }

    return true;
}

WindowInfo SwayWindowManager::find_focused(const nlohmann::json& node) {
    if (node.value("focused", false)) {
        WindowInfo info;
        info.app_id = node.value("app_id", "");
        if (info.app_id.empty() && node.contains("window_properties")) {
            info.window_class = node["window_properties"].value("class", "");
        }
        info.title = node.value("name", "");
        info.pid = node.value("pid", 0);
        return info;
    }

    if (node.contains("nodes")) {
        for (auto& child : node["nodes"]) {
            auto info = find_focused(child);
            if (!info.empty() || child.value("focused", false)) return info;
        }
    }
    if (node.contains("floating_nodes")) {
        for (auto& child : node["floating_nodes"]) {
            auto info = find_focused(child);
            if (!info.empty() || child.value("focused", false)) return info;
        }
    }
    return {};
}
