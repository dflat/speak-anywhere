#include <catch2/catch_test_macros.hpp>

#include "wav_encoder.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Read a little-endian uint16 from raw bytes.
uint16_t read_u16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

// Read a little-endian uint32 from raw bytes.
uint32_t read_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

std::string read_tag(const uint8_t* p) {
    return {reinterpret_cast<const char*>(p), 4};
}

} // namespace

TEST_CASE("wav::encode", "[wav]") {
    constexpr uint32_t sample_rate = 16000;
    std::vector<int16_t> samples = {0, 100, -100, 32767, -32768};

    SECTION("HeaderMagic") {
        auto wav = wav::encode(samples, sample_rate);
        REQUIRE(read_tag(wav.data()) == "RIFF");
        REQUIRE(read_tag(wav.data() + 8) == "WAVE");
        REQUIRE(read_tag(wav.data() + 12) == "fmt ");
        REQUIRE(read_tag(wav.data() + 36) == "data");
    }

    SECTION("HeaderSize") {
        auto wav = wav::encode(samples, sample_rate);
        REQUIRE(wav.size() == 44 + samples.size() * 2);
    }

    SECTION("HeaderFields") {
        auto wav = wav::encode(samples, sample_rate);

        // fmt chunk size
        REQUIRE(read_u32(wav.data() + 16) == 16);
        // PCM format
        REQUIRE(read_u16(wav.data() + 20) == 1);
        // channels
        REQUIRE(read_u16(wav.data() + 22) == 1);
        // sample rate
        REQUIRE(read_u32(wav.data() + 24) == sample_rate);
        // byte rate = sample_rate * channels * bits/8
        REQUIRE(read_u32(wav.data() + 28) == sample_rate * 1 * 16 / 8);
        // block align
        REQUIRE(read_u16(wav.data() + 32) == 2);
        // bits per sample
        REQUIRE(read_u16(wav.data() + 34) == 16);
        // data chunk size
        uint32_t data_size = static_cast<uint32_t>(samples.size() * 2);
        REQUIRE(read_u32(wav.data() + 40) == data_size);
        // RIFF chunk size = file_size - 8 = 36 + data_size
        REQUIRE(read_u32(wav.data() + 4) == 36 + data_size);
    }

    SECTION("DataIntegrity") {
        auto wav = wav::encode(samples, sample_rate);
        auto* data_ptr = reinterpret_cast<const int16_t*>(wav.data() + 44);
        for (size_t i = 0; i < samples.size(); ++i) {
            REQUIRE(data_ptr[i] == samples[i]);
        }
    }

    SECTION("EmptySamples") {
        std::vector<int16_t> empty;
        auto wav = wav::encode(empty, sample_rate);
        REQUIRE(wav.size() == 44);
        REQUIRE(read_u32(wav.data() + 40) == 0);
    }
}
