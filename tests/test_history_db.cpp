#include <catch2/catch_test_macros.hpp>

#include "storage/history_db.hpp"
#include "sway/window_info.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {

struct TmpDb {
    std::string path;

    TmpDb() {
        path = std::filesystem::temp_directory_path() /
               ("sa_test_db_" + std::to_string(getpid()) + ".sqlite");
    }

    ~TmpDb() { std::filesystem::remove(path); }
};

} // namespace

TEST_CASE("HistoryDb", "[history]") {

    SECTION("OpenCreatesFile") {
        TmpDb tmp;
        HistoryDb db;
        REQUIRE(db.open(tmp.path));
        REQUIRE(std::filesystem::exists(tmp.path));
    }

    SECTION("InsertAndRetrieve") {
        TmpDb tmp;
        HistoryDb db;
        REQUIRE(db.open(tmp.path));

        WindowInfo ctx{.app_id = "kitty", .window_class = "", .title = "terminal"};
        REQUIRE(db.insert("hello world", 2.5, 0.3, ctx, "lan"));

        auto entries = db.recent(1);
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].text == "hello world");
        REQUIRE(entries[0].audio_duration == 2.5);
        REQUIRE(entries[0].processing_time == 0.3);
        REQUIRE(entries[0].app_id == "kitty");
        REQUIRE(entries[0].window_title == "terminal");
        REQUIRE(entries[0].backend == "lan");
    }

    SECTION("X11WindowClass") {
        TmpDb tmp;
        HistoryDb db;
        REQUIRE(db.open(tmp.path));

        WindowInfo ctx{.app_id = "", .window_class = "Firefox", .title = "browser"};
        REQUIRE(db.insert("browser test", 1.0, 0.1, ctx, "lan"));

        auto entries = db.recent(1);
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].app_id.empty());
        REQUIRE(entries[0].window_class == "Firefox");
        REQUIRE(entries[0].window_title == "browser");
    }

    SECTION("LimitWorks") {
        TmpDb tmp;
        HistoryDb db;
        REQUIRE(db.open(tmp.path));

        WindowInfo ctx;
        for (int i = 0; i < 5; ++i) {
            REQUIRE(db.insert("entry " + std::to_string(i), 1.0, 0.1, ctx, "lan"));
        }

        auto entries = db.recent(2);
        REQUIRE(entries.size() == 2);
    }

    SECTION("ReverseChronological") {
        TmpDb tmp;
        HistoryDb db;
        REQUIRE(db.open(tmp.path));

        WindowInfo ctx;
        REQUIRE(db.insert("first", 1.0, 0.1, ctx, "lan"));
        REQUIRE(db.insert("second", 1.0, 0.1, ctx, "lan"));
        REQUIRE(db.insert("third", 1.0, 0.1, ctx, "lan"));

        auto entries = db.recent(3);
        REQUIRE(entries.size() == 3);
        REQUIRE(entries[0].text == "third");
        REQUIRE(entries[1].text == "second");
        REQUIRE(entries[2].text == "first");
    }

    SECTION("NullableFields") {
        TmpDb tmp;
        HistoryDb db;
        REQUIRE(db.open(tmp.path));

        // Empty strings should be stored as NULL, retrieved as empty
        WindowInfo ctx;
        REQUIRE(db.insert("test", 1.0, 0.1, ctx, "lan"));

        auto entries = db.recent(1);
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].app_id.empty());
        REQUIRE(entries[0].agent.empty());
        REQUIRE(entries[0].working_dir.empty());
    }

    SECTION("TimestampAutoPopulated") {
        TmpDb tmp;
        HistoryDb db;
        REQUIRE(db.open(tmp.path));

        WindowInfo ctx;
        REQUIRE(db.insert("test", 1.0, 0.1, ctx, "lan"));

        auto entries = db.recent(1);
        REQUIRE(entries.size() == 1);
        REQUIRE_FALSE(entries[0].timestamp.empty());
    }
}
