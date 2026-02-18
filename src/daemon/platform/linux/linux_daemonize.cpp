#include "platform/daemonizer.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <print>
#include <unistd.h>

namespace platform {

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        std::println(stderr, "fork() failed: {}", std::strerror(errno));
        _exit(1);
    }
    if (pid > 0) _exit(0);

    setsid();

    pid = fork();
    if (pid < 0) _exit(1);
    if (pid > 0) _exit(0);

    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

} // namespace platform
