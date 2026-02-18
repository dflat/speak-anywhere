#include <catch2/catch_test_macros.hpp>

#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

// RAII temp file that auto-deletes.
struct TmpFile {
    std::string path;

    explicit TmpFile(const std::string& content) {
        path = std::filesystem::temp_directory_path() / "sa_test_config_XXXXXX";
        // mkstemp needs a mutable char*
        std::vector<char> tmpl(path.begin(), path.end());
        tmpl.push_back('\0');
        int fd = mkstemp(tmpl.data());
        path.assign(tmpl.data());
        ::write(fd, content.data(), content.size());
        ::close(fd);
    }

    ~TmpFile() { std::filesystem::remove(path); }
};

} // namespace

TEST_CASE("Config", "[config]") {

    SECTION("DefaultValues") {
        Config cfg;
        REQUIRE(cfg.backend.type == "lan");
        REQUIRE(cfg.backend.url == "http://localhost:8080");
        REQUIRE(cfg.backend.api_format == "whisper.cpp");
        REQUIRE(cfg.backend.language == "en");
        REQUIRE(cfg.output.default_method == "clipboard");
        REQUIRE(cfg.audio.sample_rate == 16000);
        REQUIRE(cfg.audio.max_seconds == 120);
        REQUIRE(cfg.audio.ring_buffer_bytes() == 120 * 16000 * sizeof(int16_t));
        REQUIRE(cfg.agents.size() == 4);
    }

    SECTION("LoadFullConfig") {
        TmpFile f(R"({
            "backend": {
                "type": "remote",
                "url": "http://10.0.0.1:9090",
                "api_format": "openai",
                "language": "de"
            },
            "output": { "default": "type" },
            "audio": { "sample_rate": 48000, "max_seconds": 60 },
            "agents": ["nvim", "emacs"]
        })");

        auto cfg = Config::load(f.path);
        REQUIRE(cfg.backend.type == "remote");
        REQUIRE(cfg.backend.url == "http://10.0.0.1:9090");
        REQUIRE(cfg.backend.api_format == "openai");
        REQUIRE(cfg.backend.language == "de");
        REQUIRE(cfg.output.default_method == "type");
        REQUIRE(cfg.audio.sample_rate == 48000);
        REQUIRE(cfg.audio.max_seconds == 60);
        REQUIRE(cfg.agents == std::vector<std::string>{"nvim", "emacs"});
    }

    SECTION("LoadPartialConfig") {
        TmpFile f(R"({ "backend": { "language": "fr" } })");

        auto cfg = Config::load(f.path);
        REQUIRE(cfg.backend.language == "fr");
        // Other fields retain defaults
        REQUIRE(cfg.backend.type == "lan");
        REQUIRE(cfg.backend.url == "http://localhost:8080");
        REQUIRE(cfg.output.default_method == "clipboard");
        REQUIRE(cfg.audio.sample_rate == 16000);
    }

    SECTION("LoadInvalidJson") {
        TmpFile f("not json {{{");

        auto cfg = Config::load(f.path);
        // Falls back to defaults
        REQUIRE(cfg.backend.type == "lan");
        REQUIRE(cfg.audio.sample_rate == 16000);
    }

    SECTION("LoadMissingFile") {
        auto cfg = Config::load("/tmp/sa_test_nonexistent_config_file.json");
        REQUIRE(cfg.backend.type == "lan");
        REQUIRE(cfg.audio.sample_rate == 16000);
    }
}
