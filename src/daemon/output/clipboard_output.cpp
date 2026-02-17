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
    ssize_t written = ::write(pipefd[1], text.data(), text.size());
    ::close(pipefd[1]);

    if (written < 0) {
        ::waitpid(pid, nullptr, 0);
        return std::unexpected(std::string("write() failed: ") + std::strerror(errno));
    }

    int status;
    ::waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        return std::unexpected("wl-copy exited with code " + std::to_string(WEXITSTATUS(status)));
    }

    return {};
}
