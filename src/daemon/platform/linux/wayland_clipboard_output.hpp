#pragma once

#include "output/output.hpp"

class WaylandClipboardOutput : public OutputMethod {
public:
    std::expected<void, std::string> deliver(const std::string& text) override;
};
