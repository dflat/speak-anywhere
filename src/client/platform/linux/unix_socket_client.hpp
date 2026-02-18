#pragma once

#include "platform/ipc_client.hpp"

class UnixSocketClient : public IpcClient {
public:
    UnixSocketClient();
    ~UnixSocketClient() override;

    UnixSocketClient(const UnixSocketClient&) = delete;
    UnixSocketClient& operator=(const UnixSocketClient&) = delete;

    bool connect(const std::string& endpoint) override;
    bool send(const nlohmann::json& cmd) override;
    bool recv(nlohmann::json& response, int timeout_ms = 30000) override;
    void close() override;

private:
    int fd_ = -1;
};
