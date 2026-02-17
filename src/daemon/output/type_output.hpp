#pragma once

#include "output.hpp"

class TypeOutput : public OutputMethod {
public:
    // If is_terminal is true, uses clipboard + paste shortcut instead of character typing.
    explicit TypeOutput(bool is_terminal = false);
    std::expected<void, std::string> deliver(const std::string& text) override;

private:
    bool is_terminal_;

    std::expected<void, std::string> type_direct(const std::string& text);
    std::expected<void, std::string> clipboard_paste(const std::string& text);
};
