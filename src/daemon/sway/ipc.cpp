#include "ipc.hpp"

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <print>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

SwayIpc::SwayIpc() = default;

SwayIpc::~SwayIpc() {
    if (query_fd_ >= 0) ::close(query_fd_);
    if (event_fd_ >= 0) ::close(event_fd_);
}

bool SwayIpc::connect() {
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

bool SwayIpc::subscribe_window_events() {
    event_fd_ = connect_socket(sway_sock_);
    if (event_fd_ < 0) return false;

    if (!send_message(event_fd_, MSG_SUBSCRIBE, R"(["window"])")) {
        ::close(event_fd_);
        event_fd_ = -1;
        return false;
    }

    // Read subscribe response
    uint32_t type;
    std::string payload;
    if (!recv_message(event_fd_, type, payload)) {
        ::close(event_fd_);
        event_fd_ = -1;
        return false;
    }

    return true;
}

WindowInfo SwayIpc::get_focused_window() {
    if (query_fd_ < 0) return {};

    if (!send_message(query_fd_, MSG_GET_TREE)) return {};

    uint32_t type;
    std::string payload;
    if (!recv_message(query_fd_, type, payload)) return {};

    try {
        auto tree = nlohmann::json::parse(payload);
        return find_focused(tree);
    } catch (...) {
        return {};
    }
}

bool SwayIpc::read_event(WindowInfo& info) {
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
            info.title = c.value("name", "");
            info.pid = c.value("pid", 0);
        }
        return true;
    } catch (...) {
        return false;
    }
}

int SwayIpc::connect_socket(const std::string& path) {
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

bool SwayIpc::send_message(int fd, uint32_t type, const std::string& payload) {
    // Header: "i3-ipc" (6 bytes) + length (4 bytes) + type (4 bytes)
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

bool SwayIpc::recv_message(int fd, uint32_t& type, std::string& payload) {
    // Read header
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

WindowInfo SwayIpc::find_focused(const nlohmann::json& node) {
    if (node.value("focused", false)) {
        WindowInfo info;
        info.app_id = node.value("app_id", "");
        info.title = node.value("name", "");
        info.pid = node.value("pid", 0);
        return info;
    }

    if (node.contains("nodes")) {
        for (auto& child : node["nodes"]) {
            auto info = find_focused(child);
            if (!info.empty()) return info;
        }
    }
    if (node.contains("floating_nodes")) {
        for (auto& child : node["floating_nodes"]) {
            auto info = find_focused(child);
            if (!info.empty()) return info;
        }
    }
    return {};
}
