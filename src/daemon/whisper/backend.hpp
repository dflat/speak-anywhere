#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>

struct TranscriptResult {
    std::string text;
    double duration_s = 0.0;
    double processing_s = 0.0;
};

class WhisperBackend {
public:
    virtual ~WhisperBackend() = default;
    virtual std::expected<TranscriptResult, std::string>
        transcribe(std::span<const int16_t> audio, uint32_t sample_rate) = 0;
};
