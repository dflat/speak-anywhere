#pragma once

#include <nlohmann/json.hpp>
#include <string>

class IpcClient {
public:
    IpcClient();
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    bool connect(const std::string& socket_path);
    bool send(const nlohmann::json& cmd);
    bool recv(nlohmann::json& response, int timeout_ms = 30000);
    void close();

    // Get the default socket path.
    static std::string default_socket_path();

private:
    int fd_ = -1;
};
