#include "platform/platform_paths.hpp"

#include <cstdlib>

namespace platform {

std::string config_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg) return std::string(xdg) + "/speak-anywhere";
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return std::string(home) + "/.config/speak-anywhere";
}

std::string data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg) return std::string(xdg) + "/speak-anywhere";
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return std::string(home) + "/.local/share/speak-anywhere";
}

std::string ipc_endpoint() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg) return std::string(xdg) + "/speak-anywhere.sock";
    return "/tmp/speak-anywhere.sock";
}

} // namespace platform
