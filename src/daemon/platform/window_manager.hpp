#pragma once

#include "sway/window_info.hpp"

class WindowManager {
public:
    virtual ~WindowManager() = default;
    virtual bool connect() = 0;
    virtual bool subscribe_focus_events() = 0;
    virtual WindowInfo get_focused_window() = 0;
    virtual int event_fd() const = 0;
    virtual bool read_event(WindowInfo& info) = 0;
};
