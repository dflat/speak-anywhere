#include "config.hpp"

#include "platform/platform_paths.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <print>

namespace fs = std::filesystem;
using json = nlohmann::json;

Config Config::load(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::println(stderr, "config: could not open {}, using defaults", path);
        return cfg;
    }

    try {
        auto j = json::parse(f);

        if (j.contains("backend")) {
            auto& b = j["backend"];
            if (b.contains("type")) cfg.backend.type = b["type"].get<std::string>();
            if (b.contains("url")) cfg.backend.url = b["url"].get<std::string>();
            if (b.contains("api_format")) cfg.backend.api_format = b["api_format"].get<std::string>();
            if (b.contains("language")) cfg.backend.language = b["language"].get<std::string>();
        }

        if (j.contains("output")) {
            auto& o = j["output"];
            if (o.contains("default")) cfg.output.default_method = o["default"].get<std::string>();
        }

        if (j.contains("audio")) {
            auto& a = j["audio"];
            if (a.contains("sample_rate")) cfg.audio.sample_rate = a["sample_rate"].get<uint32_t>();
            if (a.contains("max_seconds")) cfg.audio.max_seconds = a["max_seconds"].get<uint32_t>();
        }

        if (j.contains("agents")) {
            cfg.agents = j["agents"].get<std::vector<std::string>>();
        }

    } catch (const json::exception& e) {
        std::println(stderr, "config: parse error: {}", e.what());
    }

    return cfg;
}

Config Config::load_default() {
    auto dir = platform::config_dir();
    if (dir.empty()) return Config{};

    auto config_path = fs::path(dir) / "config.json";
    if (fs::exists(config_path)) {
        return load(config_path.string());
    }
    return Config{};
}
