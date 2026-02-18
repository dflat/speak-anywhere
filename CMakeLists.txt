cmake_minimum_required(VERSION 3.25)
project(speak-anywhere VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

find_path(NLOHMANN_JSON_INCLUDE nlohmann/json.hpp REQUIRED)

# Portable daemon sources (no platform dependencies)
set(PORTABLE_SOURCES
    src/daemon/daemon_core.cpp
    src/daemon/session.cpp
    src/daemon/config.cpp
    src/daemon/whisper/lan_backend.cpp
    src/daemon/storage/history_db.cpp
)

# Platform-conditional sources
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

    set(PLATFORM_SOURCES
        src/daemon/platform/linux/pipewire_capture.cpp
        src/daemon/platform/linux/sway_window_manager.cpp
        src/daemon/platform/linux/procfs_detector.cpp
        src/daemon/platform/linux/unix_socket_server.cpp
        src/daemon/platform/linux/linux_event_loop.cpp
        src/daemon/platform/linux/linux_paths.cpp
        src/daemon/platform/linux/linux_daemonize.cpp
        src/daemon/platform/linux/wayland_clipboard_output.cpp
        src/daemon/platform/linux/wayland_type_output.cpp
    )
    set(PLATFORM_LINK_LIBS PkgConfig::PIPEWIRE)
    set(CLIENT_PLATFORM_SOURCES
        src/client/platform/linux/unix_socket_client.cpp
        src/daemon/platform/linux/linux_paths.cpp
    )
endif()

# Daemon library (everything except main.cpp — shared by daemon and tests)
add_library(speak-anywhere-lib STATIC
    ${PORTABLE_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(speak-anywhere-lib PUBLIC
    src/daemon
    ${NLOHMANN_JSON_INCLUDE}
)

target_link_libraries(speak-anywhere-lib PUBLIC
    ${PLATFORM_LINK_LIBS}
    PkgConfig::CURL
    PkgConfig::SQLITE3
)

# Daemon executable
add_executable(speak-anywhere src/daemon/main.cpp)
target_link_libraries(speak-anywhere PRIVATE speak-anywhere-lib)

# CLI client
add_executable(sa
    src/client/main.cpp
    ${CLIENT_PLATFORM_SOURCES}
)

target_include_directories(sa PRIVATE
    src/client
    src/daemon
    ${NLOHMANN_JSON_INCLUDE}
)

# Tests
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

enable_testing()

add_executable(tests
    tests/test_ring_buffer.cpp
    tests/test_wav_encoder.cpp
    tests/test_session.cpp
    tests/test_config.cpp
    tests/test_ipc_protocol.cpp
    tests/test_history_db.cpp
    tests/test_agent_detector.cpp
    tests/test_window_info.cpp
)

target_link_libraries(tests PRIVATE
    speak-anywhere-lib
    Catch2::Catch2WithMain
)

# IPC protocol test needs the client too
target_sources(tests PRIVATE ${CLIENT_PLATFORM_SOURCES})
target_include_directories(tests PRIVATE src/client)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CatchSharedTests OPTIONAL)
include(Catch)
catch_discover_tests(tests)
