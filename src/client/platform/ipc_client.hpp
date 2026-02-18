#pragma once

#include <nlohmann/json.hpp>
#include <string>

class IpcClient {
public:
    virtual ~IpcClient() = default;
    virtual bool connect(const std::string& endpoint) = 0;
    virtual bool send(const nlohmann::json& cmd) = 0;
    virtual bool recv(nlohmann::json& response, int timeout_ms = 30000) = 0;
    virtual void close() = 0;
};
