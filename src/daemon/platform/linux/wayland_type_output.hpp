#pragma once

#include "output/output.hpp"

class WaylandTypeOutput : public OutputMethod {
public:
    explicit WaylandTypeOutput(bool is_terminal = false, uint32_t delay_ms = 50);
    std::expected<void, std::string> deliver(const std::string& text) override;

private:
    bool is_terminal_;
    uint32_t delay_ms_;

    std::expected<void, std::string> terminal_paste(const std::string& text);
    std::expected<void, std::string> general_paste(const std::string& text);
};
