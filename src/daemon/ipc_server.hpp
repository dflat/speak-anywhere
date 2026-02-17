#pragma once

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class IpcServer {
public:
    using CommandHandler = std::function<nlohmann::json(const nlohmann::json&)>;

    IpcServer();
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start(const std::string& socket_path);
    void stop();

    int server_fd() const { return server_fd_; }

    // Accept a new client connection. Returns client fd or -1.
    int accept_client();

    // Read a command from a client. Returns true if a complete message was read.
    // Returns false if client disconnected or incomplete.
    bool read_command(int client_fd, nlohmann::json& cmd);

    // Send response to client.
    bool send_response(int client_fd, const nlohmann::json& response);

    // Close and remove a client.
    void close_client(int client_fd);

private:
    int server_fd_ = -1;
    std::string socket_path_;

    // Per-client read buffers for partial reads.
    struct ClientBuffer {
        int fd;
        std::string buf;
    };
    std::vector<ClientBuffer> clients_;

    ClientBuffer* find_client(int fd);
};
