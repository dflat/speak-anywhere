#include "history_db.hpp"

#include <cstdlib>
#include <filesystem>
#include <print>

namespace fs = std::filesystem;

HistoryDb::HistoryDb() = default;

HistoryDb::~HistoryDb() {
    close();
}

bool HistoryDb::open(const std::string& path) {
    // Ensure parent directory exists
    fs::path p(path);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::println(stderr, "db: failed to open {}: {}", path, sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }

    // Enable WAL mode for better concurrent access
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    if (!create_tables()) return false;

    // Prepare statements
    const char* insert_sql =
        "INSERT INTO transcriptions (text, audio_duration, processing_time, "
        "app_context, app_id, window_title, agent, working_dir, backend) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";

    const char* recent_sql =
        "SELECT id, timestamp, text, audio_duration, processing_time, "
        "app_context, app_id, window_title, agent, working_dir, backend "
        "FROM transcriptions ORDER BY id DESC LIMIT ?";

    if (sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt_, nullptr) != SQLITE_OK) {
        std::println(stderr, "db: prepare insert failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    if (sqlite3_prepare_v2(db_, recent_sql, -1, &recent_stmt_, nullptr) != SQLITE_OK) {
        std::println(stderr, "db: prepare recent failed: {}", sqlite3_errmsg(db_));
        return false;
    }

    return true;
}

void HistoryDb::close() {
    if (insert_stmt_) { sqlite3_finalize(insert_stmt_); insert_stmt_ = nullptr; }
    if (recent_stmt_) { sqlite3_finalize(recent_stmt_); recent_stmt_ = nullptr; }
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

bool HistoryDb::insert(const std::string& text, double audio_duration, double processing_time,
                       const WindowInfo& ctx, const std::string& backend) {
    if (!insert_stmt_) return false;

    sqlite3_reset(insert_stmt_);
    sqlite3_bind_text(insert_stmt_, 1, text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(insert_stmt_, 2, audio_duration);
    sqlite3_bind_double(insert_stmt_, 3, processing_time);

    auto bind_nullable = [this](int idx, const std::string& val) {
        if (val.empty()) sqlite3_bind_null(insert_stmt_, idx);
        else sqlite3_bind_text(insert_stmt_, idx, val.c_str(), -1, SQLITE_TRANSIENT);
    };

    bind_nullable(4, ctx.context);
    bind_nullable(5, ctx.app_id);
    bind_nullable(6, ctx.title);
    bind_nullable(7, ctx.agent);
    bind_nullable(8, ctx.working_dir);
    bind_nullable(9, backend);

    int rc = sqlite3_step(insert_stmt_);
    if (rc != SQLITE_DONE) {
        std::println(stderr, "db: insert failed: {}", sqlite3_errmsg(db_));
        return false;
    }
    return true;
}

std::vector<HistoryEntry> HistoryDb::recent(int limit) {
    std::vector<HistoryEntry> entries;
    if (!recent_stmt_) return entries;

    sqlite3_reset(recent_stmt_);
    sqlite3_bind_int(recent_stmt_, 1, limit);

    auto get_text = [](sqlite3_stmt* stmt, int col) -> std::string {
        auto* p = sqlite3_column_text(stmt, col);
        return p ? reinterpret_cast<const char*>(p) : "";
    };

    while (sqlite3_step(recent_stmt_) == SQLITE_ROW) {
        HistoryEntry e;
        e.id = sqlite3_column_int64(recent_stmt_, 0);
        e.timestamp = get_text(recent_stmt_, 1);
        e.text = get_text(recent_stmt_, 2);
        e.audio_duration = sqlite3_column_double(recent_stmt_, 3);
        e.processing_time = sqlite3_column_double(recent_stmt_, 4);
        e.app_context = get_text(recent_stmt_, 5);
        e.app_id = get_text(recent_stmt_, 6);
        e.window_title = get_text(recent_stmt_, 7);
        e.agent = get_text(recent_stmt_, 8);
        e.working_dir = get_text(recent_stmt_, 9);
        e.backend = get_text(recent_stmt_, 10);
        entries.push_back(std::move(e));
    }

    return entries;
}

bool HistoryDb::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS transcriptions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f','now')),
            text TEXT NOT NULL,
            audio_duration REAL,
            processing_time REAL,
            app_context TEXT,
            app_id TEXT,
            window_title TEXT,
            agent TEXT,
            working_dir TEXT,
            backend TEXT
        );
    )";

    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::println(stderr, "db: create table failed: {}", err ? err : "unknown");
        sqlite3_free(err);
        return false;
    }
    return true;
}
