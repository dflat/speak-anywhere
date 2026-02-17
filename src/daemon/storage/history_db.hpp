#pragma once

#include "../sway/window_info.hpp"

#include <cstdint>
#include <sqlite3.h>
#include <string>
#include <vector>

struct HistoryEntry {
    int64_t id;
    std::string timestamp;
    std::string text;
    double audio_duration;
    double processing_time;
    std::string app_context;
    std::string app_id;
    std::string window_title;
    std::string agent;
    std::string working_dir;
    std::string backend;
};

class HistoryDb {
public:
    HistoryDb();
    ~HistoryDb();

    HistoryDb(const HistoryDb&) = delete;
    HistoryDb& operator=(const HistoryDb&) = delete;

    bool open(const std::string& path);
    void close();

    bool insert(const std::string& text, double audio_duration, double processing_time,
                const WindowInfo& context, const std::string& backend);

    std::vector<HistoryEntry> recent(int limit = 10);

private:
    bool create_tables();

    sqlite3* db_ = nullptr;
    sqlite3_stmt* insert_stmt_ = nullptr;
    sqlite3_stmt* recent_stmt_ = nullptr;
};
