#include "ipc_client.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

IpcClient::IpcClient() = default;

IpcClient::~IpcClient() {
    close();
}

bool IpcClient::connect(const std::string& socket_path) {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

bool IpcClient::send(const nlohmann::json& cmd) {
    if (fd_ < 0) return false;
    std::string msg = cmd.dump() + "\n";
    ssize_t sent = ::send(fd_, msg.data(), msg.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(msg.size());
}

bool IpcClient::recv(nlohmann::json& response, int timeout_ms) {
    if (fd_ < 0) return false;

    pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
    std::string buf;

    while (true) {
        int ret = ::poll(&pfd, 1, timeout_ms);
        if (ret <= 0) return false;

        char tmp[4096];
        ssize_t n = ::recv(fd_, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;

        buf.append(tmp, static_cast<size_t>(n));
        auto pos = buf.find('\n');
        if (pos != std::string::npos) {
            try {
                response = nlohmann::json::parse(buf.substr(0, pos));
                return true;
            } catch (...) {
                return false;
            }
        }
    }
}

void IpcClient::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

std::string IpcClient::default_socket_path() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg) {
        return std::string(xdg) + "/speak-anywhere.sock";
    }
    return "/tmp/speak-anywhere.sock";
}
