#include "clipboard_output.hpp"

#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

std::expected<void, std::string> ClipboardOutput::deliver(const std::string& text) {
    int pipefd[2];
    if (::pipe(pipefd) < 0) {
        return std::unexpected(std::string("pipe() failed: ") + std::strerror(errno));
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return std::unexpected(std::string("fork() failed: ") + std::strerror(errno));
    }

    if (pid == 0) {
        // Child: redirect stdin from pipe, exec wl-copy
        ::close(pipefd[1]);
        ::dup2(pipefd[0], STDIN_FILENO);
        ::close(pipefd[0]);
        ::execlp("wl-copy", "wl-copy", nullptr);
        ::_exit(127);
    }

    // Parent: write text to pipe
    ::close(pipefd[0]);
    size_t total_written = 0;
    while (total_written < text.size()) {
        ssize_t n = ::write(pipefd[1], text.data() + total_written, text.size() - total_written);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(pipefd[1]);
            ::waitpid(pid, nullptr, 0);
            return std::unexpected(std::string("write() failed: ") + std::strerror(errno));
        }
        total_written += static_cast<size_t>(n);
    }
    ::close(pipefd[1]);

    int status;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return std::unexpected(std::string("waitpid() failed: ") + std::strerror(errno));
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        return std::unexpected("wl-copy exited with code " + std::to_string(WEXITSTATUS(status)));
    }

    return {};
}
