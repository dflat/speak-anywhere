#pragma once

#include "output.hpp"

class ClipboardOutput : public OutputMethod {
public:
    std::expected<void, std::string> deliver(const std::string& text) override;
};
