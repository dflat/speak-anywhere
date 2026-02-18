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
        return terminal_paste(text);
    }
    return general_paste(text);
}

std::expected<void, std::string> TypeOutput::type_direct(const std::string& text) {
    pid_t pid = ::fork();
    if (pid < 0) {
        return std::unexpected(std::string("fork() failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        // -d 50 adds a small delay between characters to avoid overwhelming some apps
        ::execlp("wtype", "wtype", "-d", "10", text.c_str(), nullptr);
        ::_exit(127);
    }

    int status;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return std::unexpected(std::string("waitpid() failed: ") + std::strerror(errno));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        return std::unexpected("wtype exited with code " + std::to_string(WEXITSTATUS(status)));
    }

    return {};
}

std::expected<void, std::string> TypeOutput::terminal_paste(const std::string& text) {
    // First, copy to clipboard
    ClipboardOutput clip;
    auto res = clip.deliver(text);
    if (!res) return res;

    // Small delay to ensure wl-copy has ownership of the clipboard
    ::usleep(10000); // 10ms

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
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return std::unexpected(std::string("waitpid() failed: ") + std::strerror(errno));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        return std::unexpected("wtype terminal paste failed with code " + std::to_string(WEXITSTATUS(status)));
    }

    return {};
}

std::expected<void, std::string> TypeOutput::general_paste(const std::string& text) {
    // For non-terminal apps (browsers, etc), Ctrl+V is the standard.
    // We use clipboard as an intermediary because direct typing (wtype string) 
    // is often ignored by complex XWayland/GTK/Qt apps or mangled by layout issues.
    
    ClipboardOutput clip;
    auto res = clip.deliver(text);
    if (!res) return res;

    ::usleep(10000); // 10ms

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
