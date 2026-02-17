#include "type_output.hpp"
#include "clipboard_output.hpp"

#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

TypeOutput::TypeOutput(bool is_terminal)
    : is_terminal_(is_terminal) {}

std::expected<void, std::string> TypeOutput::deliver(const std::string& text) {
    if (is_terminal_) {
        return clipboard_paste(text);
    }
    return type_direct(text);
}

std::expected<void, std::string> TypeOutput::type_direct(const std::string& text) {
    pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(std::string("fork() failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        ::execlp("wtype", "wtype", "-d", "0", text.c_str(), nullptr);
        ::_exit(127);
    }

    int status;
    ::waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        return std::unexpected("wtype exited with code " + std::to_string(WEXITSTATUS(status)));
    }

    return {};
}

std::expected<void, std::string> TypeOutput::clipboard_paste(const std::string& text) {
    // First, copy to clipboard
    ClipboardOutput clip;
    auto res = clip.deliver(text);
    if (!res) return res;

    // Then simulate Ctrl+Shift+V paste shortcut
    pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(std::string("fork() failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        ::execlp("wtype", "wtype", "-M", "ctrl", "-M", "shift", "-k", "v", nullptr);
        ::_exit(127);
    }

    int status;
    ::waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        return std::unexpected("wtype paste failed with code " + std::to_string(WEXITSTATUS(status)));
    }

    return {};
}
