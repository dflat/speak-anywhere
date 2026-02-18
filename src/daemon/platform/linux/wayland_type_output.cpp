#include "platform/linux/wayland_type_output.hpp"
#include "platform/linux/wayland_clipboard_output.hpp"

#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

WaylandTypeOutput::WaylandTypeOutput(bool is_terminal)
    : is_terminal_(is_terminal) {}

std::expected<void, std::string> WaylandTypeOutput::deliver(const std::string& text) {
    if (is_terminal_) {
        return terminal_paste(text);
    }
    return general_paste(text);
}

std::expected<void, std::string> WaylandTypeOutput::terminal_paste(const std::string& text) {
    WaylandClipboardOutput clip;
    auto res = clip.deliver(text);
    if (!res) return res;

    ::usleep(10000);

    pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(std::string("fork() failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        ::execlp("wtype", "wtype", "-M", "ctrl", "-M", "shift", "-k", "v", nullptr);
        ::_exit(127);
    }

    int status;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return std::unexpected(std::string("waitpid() failed: ") + std::strerror(errno));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        return std::unexpected("wtype terminal paste failed with code " + std::to_string(WEXITSTATUS(status)));
    }

    return {};
}

std::expected<void, std::string> WaylandTypeOutput::general_paste(const std::string& text) {
    WaylandClipboardOutput clip;
    auto res = clip.deliver(text);
    if (!res) return res;

    ::usleep(10000);

    pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(std::string("fork() failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        ::execlp("wtype", "wtype", "-M", "ctrl", "-k", "v", nullptr);
        ::_exit(127);
    }

    int status;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return std::unexpected(std::string("waitpid() failed: ") + std::strerror(errno));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        return std::unexpected("wtype general paste failed with code " + std::to_string(WEXITSTATUS(status)));
    }

    return {};
}
