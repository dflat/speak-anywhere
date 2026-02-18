#include <catch2/catch_test_macros.hpp>

#include "sway/window_info.hpp"

TEST_CASE("WindowInfo", "[window]") {

    SECTION("DefaultIsEmpty") {
        WindowInfo info;
        REQUIRE(info.empty());
    }

    SECTION("WithAppIdNotEmpty") {
        WindowInfo info;
        info.app_id = "kitty";
        REQUIRE_FALSE(info.empty());
    }

    SECTION("WithWindowClassNotEmpty") {
        WindowInfo info;
        info.window_class = "Firefox";
        REQUIRE_FALSE(info.empty());
    }

    SECTION("WithTitleNotEmpty") {
        WindowInfo info;
        info.title = "Some Title";
        REQUIRE_FALSE(info.empty());
    }

    SECTION("WithPidNotEmpty") {
        WindowInfo info;
        info.pid = 1234;
        REQUIRE_FALSE(info.empty());
    }
}
