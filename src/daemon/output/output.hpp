#pragma once

#include "../platform/window_manager.hpp"

#include <expected>
#include <string>

class OutputMethod {
public:
    virtual ~OutputMethod() = default;
    virtual std::expected<void, std::string> deliver(const std::string& text, WindowManager& win) = 0;
};
