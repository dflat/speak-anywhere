#include "platform/linux/unix_socket_client.hpp"
#include "platform/platform_paths.hpp"

#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <print>
#include <string>
#include <vector>

using json = nlohmann::json;

static void usage(const char* prog) {
    std::println(stderr, "Usage: {} <command> [options]", prog);
    std::println(stderr, "Commands:");
    std::println(stderr, "  start [--output clipboard|type]   Start recording");
    std::println(stderr, "  stop                              Stop recording and transcribe");
    std::println(stderr, "  toggle [--output clipboard|type]  Toggle recording");
    std::println(stderr, "  status                            Show daemon status");
    std::println(stderr, "  history [--limit N]               Show transcription history");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    std::string output_method;
    int limit = 10;

    // Parse optional args
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) {
            output_method = argv[++i];
        } else if (arg == "--limit" && i + 1 < argc) {
            limit = std::atoi(argv[++i]);
        }
    }

    // Build command JSON
    json cmd;
    if (command == "start") {
        cmd = {{"cmd", "start"}};
        if (!output_method.empty()) cmd["output"] = output_method;
    } else if (command == "stop") {
        cmd = {{"cmd", "stop"}};
    } else if (command == "toggle") {
        cmd = {{"cmd", "toggle"}};
        if (!output_method.empty()) cmd["output"] = output_method;
    } else if (command == "status") {
        cmd = {{"cmd", "status"}};
    } else if (command == "history") {
        cmd = {{"cmd", "history"}, {"limit", limit}};
    } else {
        std::println(stderr, "Unknown command: {}", command);
        usage(argv[0]);
        return 1;
    }

    // Connect and send
    UnixSocketClient client;
    auto sock_path = platform::ipc_endpoint();

    if (!client.connect(sock_path)) {
        std::println(stderr, "Failed to connect to daemon at {}", sock_path);
        std::println(stderr, "Is speak-anywhere running?");
        return 1;
    }

    if (!client.send(cmd)) {
        std::println(stderr, "Failed to send command");
        return 1;
    }

    json response;
    if (!client.recv(response)) {
        std::println(stderr, "No response from daemon (timeout)");
        return 1;
    }

    // Display response
    auto status = response.value("status", "");

    if (command == "status") {
        auto state = response.value("state", "unknown");
        std::println("State: {}", state);
        if (response.contains("duration")) {
            std::println("Recording duration: {:.1f}s", response["duration"].get<double>());
        }
    } else if (command == "history") {
        if (response.contains("entries")) {
            for (auto& entry : response["entries"]) {
                std::println("[{}] {}", entry.value("timestamp", ""), entry.value("text", ""));
                if (entry.contains("app_context") && !entry["app_context"].is_null()) {
                    std::println("  Context: {}", entry["app_context"].get<std::string>());
                }
            }
        }
    } else if (status == "ok") {
        if (response.contains("text")) {
            std::println("{}", response["text"].get<std::string>());
        } else {
            std::println("OK");
        }
    } else if (status == "error") {
        std::println(stderr, "Error: {}", response.value("message", "unknown error"));
        return 1;
    } else {
        std::println("{}", response.dump(2));
    }

    return 0;
}
