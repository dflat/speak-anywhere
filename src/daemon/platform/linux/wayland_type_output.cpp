#include "platform/linux/wayland_type_output.hpp"
#include "platform/linux/wayland_clipboard_output.hpp"

#include <cerrno>
#include <cstring>
#include <print>
#include <sys/wait.h>
#include <unistd.h>

WaylandTypeOutput::WaylandTypeOutput(bool is_terminal, uint32_t delay_ms)
    : is_terminal_(is_terminal), delay_ms_(delay_ms) {}

std::expected<void, std::string> WaylandTypeOutput::deliver(const std::string& text, WindowManager& win) {
    if (is_terminal_) {
        return terminal_paste(text, win);
    }
    return general_paste(text, win);
}

std::expected<void, std::string> WaylandTypeOutput::terminal_paste(const std::string& text, WindowManager& win) {
    WaylandClipboardOutput clip;
    auto res = clip.deliver(text, win);
    if (!res) return res;

    ::usleep(delay_ms_ * 1000);

    // Wait for physical modifiers to be released
    int timeout = 100; // 1 second max
    if (win.is_modifier_down()) {
        std::println(stderr, "[speak-anywhere] Waiting for modifier keys to be released...");
    }
    while (win.is_modifier_down() && timeout-- > 0) {
        ::usleep(10000); // 10ms
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(std::string("fork() failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        // Neutralize Alt, Logo, and Shift anyway for good measure
        ::execlp("wtype", "wtype",
                 "-m", "alt", "-m", "logo", "-m", "shift",
                 "-M", "ctrl", "-M", "shift", "-k", "v",
                 "-m", "shift", "-m", "ctrl", nullptr);
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

std::expected<void, std::string> WaylandTypeOutput::general_paste(const std::string& text, WindowManager& win) {
    WaylandClipboardOutput clip;
    auto res = clip.deliver(text, win);
    if (!res) return res;

    ::usleep(delay_ms_ * 1000);

    // Wait for physical modifiers to be released
    int timeout = 100; // 1 second max
    if (win.is_modifier_down()) {
        std::println(stderr, "[speak-anywhere] Waiting for modifier keys to be released...");
    }
    while (win.is_modifier_down() && timeout-- > 0) {
        ::usleep(10000); // 10ms
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(std::string("fork() failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        // Neutralize Alt, Logo, and Shift anyway for good measure
        ::execlp("wtype", "wtype",
                 "-m", "alt", "-m", "logo", "-m", "shift",
                 "-M", "ctrl", "-k", "v",
                 "-m", "ctrl", nullptr);
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
