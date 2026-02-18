#include "config.hpp"
#include "platform/daemonizer.hpp"
#include "platform/linux/linux_event_loop.hpp"

#include <print>
#include <string>

int main(int argc, char* argv[]) {
    bool foreground = false;
    bool verbose = false;
    std::string config_path;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--foreground" || arg == "-f") {
            foreground = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--config" || arg == "-c") {
            if (i + 1 < argc) config_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::println("Usage: speak-anywhere [options]");
            std::println("Options:");
            std::println("  -f, --foreground    Run in foreground (don't daemonize)");
            std::println("  -v, --verbose       Enable verbose logging");
            std::println("  -c, --config PATH   Config file path");
            std::println("  -h, --help          Show this help");
            return 0;
        }
    }

    // Load config
    Config config;
    if (!config_path.empty()) {
        config = Config::load(config_path);
    } else {
        config = Config::load_default();
    }

    if (!foreground) {
        platform::daemonize();
    }

    if (verbose && foreground) {
        std::println(stderr, "[speak-anywhere] Starting (backend: {} @ {})",
                     config.backend.type, config.backend.url);
    }

    LinuxEventLoop loop(std::move(config), verbose);
    if (!loop.init()) {
        std::println(stderr, "Failed to initialize event loop");
        return 1;
    }

    loop.run();
    return 0;
}
