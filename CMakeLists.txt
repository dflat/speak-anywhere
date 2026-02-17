cmake_minimum_required(VERSION 3.25)
project(speak-anywhere VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

find_path(NLOHMANN_JSON_INCLUDE nlohmann/json.hpp REQUIRED)

# Daemon
add_executable(speak-anywhere
    src/daemon/main.cpp
    src/daemon/event_loop.cpp
    src/daemon/audio_capture.cpp
    src/daemon/session.cpp
    src/daemon/config.cpp
    src/daemon/ipc_server.cpp
    src/daemon/whisper/lan_backend.cpp
    src/daemon/sway/ipc.cpp
    src/daemon/sway/agent_detector.cpp
    src/daemon/output/clipboard_output.cpp
    src/daemon/output/type_output.cpp
    src/daemon/storage/history_db.cpp
)

target_include_directories(speak-anywhere PRIVATE
    src/daemon
    ${NLOHMANN_JSON_INCLUDE}
)

target_link_libraries(speak-anywhere PRIVATE
    PkgConfig::PIPEWIRE
    PkgConfig::CURL
    PkgConfig::SQLITE3
)

# CLI client
add_executable(sa
    src/client/main.cpp
    src/client/ipc_client.cpp
)

target_include_directories(sa PRIVATE
    ${NLOHMANN_JSON_INCLUDE}
)
