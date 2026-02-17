#pragma once

#include "backend.hpp"

#include <string>

class LanBackend : public WhisperBackend {
public:
    // api_format: "whisper.cpp" or "openai"
    LanBackend(std::string url, std::string api_format = "whisper.cpp",
               std::string language = "en");
    ~LanBackend() override;

    std::expected<TranscriptResult, std::string>
        transcribe(std::span<const int16_t> audio, uint32_t sample_rate) override;

private:
    std::string url_;
    std::string api_format_;
    std::string language_;
};
