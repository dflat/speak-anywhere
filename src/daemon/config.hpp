#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Config {
    struct Backend {
        std::string type = "lan";
        std::string url = "http://localhost:8080";
        std::string api_format = "whisper.cpp"; // "whisper.cpp" or "openai"
        std::string language = "en";
    } backend;

    struct Output {
        std::string default_method = "clipboard";
    } output;

    struct Audio {
        uint32_t sample_rate = 16000;
        uint32_t max_seconds = 120;

        // Computed from max_seconds and sample_rate (no independent config key).
        size_t ring_buffer_bytes() const {
            return static_cast<size_t>(max_seconds) * sample_rate * sizeof(int16_t);
        }
    } audio;

    std::vector<std::string> agents = {"claude", "aider", "gh", "cursor"};

    static Config load(const std::string& path);
    static Config load_default();
};
