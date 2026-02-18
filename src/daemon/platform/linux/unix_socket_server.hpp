#pragma once

#include "platform/ipc_server.hpp"

#include <string>
#include <vector>

class UnixSocketServer : public IpcServer {
public:
    UnixSocketServer();
    ~UnixSocketServer() override;

    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;

    bool start(const std::string& endpoint) override;
    void stop() override;
    int server_fd() const override { return server_fd_; }
    int accept_client() override;
    bool read_command(int client_fd, nlohmann::json& cmd) override;
    bool send_response(int client_fd, const nlohmann::json& response) override;
    void close_client(int client_fd) override;

private:
    int server_fd_ = -1;
    std::string socket_path_;

    struct ClientBuffer {
        int fd;
        std::string buf;
    };
    std::vector<ClientBuffer> clients_;

    ClientBuffer* find_client(int fd);
};
