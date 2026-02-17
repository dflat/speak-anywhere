#include "ipc_server.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <print>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

IpcServer::IpcServer() = default;

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start(const std::string& socket_path) {
    socket_path_ = socket_path;

    // Remove stale socket
    ::unlink(socket_path.c_str());

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (server_fd_ < 0) {
        std::println(stderr, "ipc: socket() failed: {}", std::strerror(errno));
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        std::println(stderr, "ipc: socket path too long");
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::println(stderr, "ipc: bind() failed: {}", std::strerror(errno));
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, 4) < 0) {
        std::println(stderr, "ipc: listen() failed: {}", std::strerror(errno));
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    return true;
}

void IpcServer::stop() {
    for (auto& c : clients_) {
        ::close(c.fd);
    }
    clients_.clear();

    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }

    if (!socket_path_.empty()) {
        ::unlink(socket_path_.c_str());
        socket_path_.clear();
    }
}

int IpcServer::accept_client() {
    int fd = ::accept4(server_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) return -1;
    clients_.push_back({fd, {}});
    return fd;
}

bool IpcServer::read_command(int client_fd, nlohmann::json& cmd) {
    auto* client = find_client(client_fd);
    if (!client) return false;

    char buf[4096];
    ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
    if (n <= 0) return false;

    client->buf.append(buf, static_cast<size_t>(n));

    // Look for newline-delimited JSON
    auto pos = client->buf.find('\n');
    if (pos == std::string::npos) return false;

    std::string line = client->buf.substr(0, pos);
    client->buf.erase(0, pos + 1);

    try {
        cmd = nlohmann::json::parse(line);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool IpcServer::send_response(int client_fd, const nlohmann::json& response) {
    std::string msg = response.dump() + "\n";
    ssize_t sent = ::send(client_fd, msg.data(), msg.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(msg.size());
}

void IpcServer::close_client(int client_fd) {
    ::close(client_fd);
    std::erase_if(clients_, [client_fd](const ClientBuffer& c) { return c.fd == client_fd; });
}

IpcServer::ClientBuffer* IpcServer::find_client(int fd) {
    auto it = std::ranges::find_if(clients_, [fd](const ClientBuffer& c) { return c.fd == fd; });
    return it != clients_.end() ? &*it : nullptr;
}
