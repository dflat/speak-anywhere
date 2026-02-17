#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// Encodes raw PCM int16 samples into a WAV file in memory.
namespace wav {

inline std::vector<uint8_t> encode(std::span<const int16_t> samples, uint32_t sample_rate) {
    constexpr uint16_t channels = 1;
    constexpr uint16_t bits_per_sample = 16;
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;
    uint32_t data_size = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    uint32_t file_size = 36 + data_size;

    std::vector<uint8_t> out(44 + data_size);
    auto w = [&out, pos = size_t(0)](const void* data, size_t len) mutable {
        std::memcpy(out.data() + pos, data, len);
        pos += len;
    };
    auto w16 = [&w](uint16_t v) { w(&v, 2); };
    auto w32 = [&w](uint32_t v) { w(&v, 4); };

    w("RIFF", 4);
    w32(file_size);
    w("WAVE", 4);
    w("fmt ", 4);
    w32(16);                // subchunk1 size
    w16(1);                 // PCM format
    w16(channels);
    w32(sample_rate);
    w32(byte_rate);
    w16(block_align);
    w16(bits_per_sample);
    w("data", 4);
    w32(data_size);
    std::memcpy(out.data() + 44, samples.data(), data_size);

    return out;
}

} // namespace wav
