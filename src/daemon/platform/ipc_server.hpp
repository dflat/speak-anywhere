#pragma once

#include <nlohmann/json.hpp>
#include <string>

class IpcServer {
public:
    virtual ~IpcServer() = default;
    virtual bool start(const std::string& endpoint) = 0;
    virtual void stop() = 0;
    virtual int server_fd() const = 0;
    virtual int accept_client() = 0;
    virtual bool read_command(int client_fd, nlohmann::json& cmd) = 0;
    virtual bool send_response(int client_fd, const nlohmann::json& response) = 0;
    virtual void close_client(int client_fd) = 0;
};
