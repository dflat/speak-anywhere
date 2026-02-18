#pragma once

#include "output/output.hpp"

class WaylandTypeOutput : public OutputMethod {
public:
    explicit WaylandTypeOutput(bool is_terminal = false);
    std::expected<void, std::string> deliver(const std::string& text) override;

private:
    bool is_terminal_;

    std::expected<void, std::string> terminal_paste(const std::string& text);
    std::expected<void, std::string> general_paste(const std::string& text);
};
