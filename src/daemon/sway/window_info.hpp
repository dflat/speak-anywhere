#pragma once

#include <string>

struct WindowInfo {
    std::string app_id;        // Wayland app_id (e.g. "kitty")
    std::string window_class;  // X11 class (e.g. "Firefox")
    std::string title;         // window title
    int pid = 0;               // window process PID
    std::string agent;         // detected CLI agent, e.g. "claude"
    std::string working_dir;   // agent's cwd
    std::string context;       // human-readable, e.g. "claude code on kitty"

    bool empty() const { return app_id.empty() && window_class.empty() && title.empty() && pid == 0; }
};
