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
}
