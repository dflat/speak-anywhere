#include <catch2/catch_test_macros.hpp>

#include "ring_buffer.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

TEST_CASE("RingBuffer", "[ring_buffer]") {
    constexpr size_t cap = 256;
    RingBuffer rb(cap);

    SECTION("WriteAndRead") {
        std::vector<uint8_t> data(64);
        std::iota(data.begin(), data.end(), uint8_t(0));

        REQUIRE(rb.write(data.data(), data.size()) == 64);
        REQUIRE(rb.available() == 64);

        std::vector<uint8_t> out(64);
        REQUIRE(rb.read(out.data(), out.size()) == 64);
        REQUIRE(out == data);
    }

    SECTION("Wraparound") {
        // Fill most of the buffer, read it, then write across the wrap boundary
        std::vector<uint8_t> fill(200);
        std::iota(fill.begin(), fill.end(), uint8_t(1));
        REQUIRE(rb.write(fill.data(), fill.size()) == 200);

        std::vector<uint8_t> sink(200);
        REQUIRE(rb.read(sink.data(), sink.size()) == 200);
        REQUIRE(sink == fill);

        // Now write_pos is at 200, read_pos is at 200. Write 128 bytes wraps around 256.
        std::vector<uint8_t> wrap(128);
        std::iota(wrap.begin(), wrap.end(), uint8_t(42));
        REQUIRE(rb.write(wrap.data(), wrap.size()) == 128);

        std::vector<uint8_t> out(128);
        REQUIRE(rb.read(out.data(), out.size()) == 128);
        REQUIRE(out == wrap);
    }

    SECTION("OverflowDrops") {
        std::vector<uint8_t> big(cap + 100);
        std::fill(big.begin(), big.end(), uint8_t(0xAB));

        size_t written = rb.write(big.data(), big.size());
        REQUIRE(written == cap);
        REQUIRE(rb.available() == cap);
    }

    SECTION("DrainAll") {
        // Write known int16_t samples as raw bytes
        std::vector<int16_t> samples = {100, -200, 300, -400, 500};
        size_t byte_len = samples.size() * sizeof(int16_t);
        REQUIRE(rb.write(samples.data(), byte_len) == byte_len);

        auto drained = rb.drain_all();
        REQUIRE(drained.size() == samples.size());
        REQUIRE(drained == samples);
    }

    SECTION("DrainAllAlignsSamples") {
        // Write an odd number of bytes — drain_all should round down to even
        std::vector<uint8_t> odd(7, 0x01);
        rb.write(odd.data(), odd.size());

        auto drained = rb.drain_all();
        // 7 bytes -> 6 bytes -> 3 samples
        REQUIRE(drained.size() == 3);
        // 1 byte left unread
        REQUIRE(rb.available() == 1);
    }

    SECTION("EmptyRead") {
        uint8_t buf[16];
        REQUIRE(rb.read(buf, sizeof(buf)) == 0);
    }

    SECTION("ResetClearsState") {
        std::vector<uint8_t> data(32, 0xFF);
        rb.write(data.data(), data.size());
        REQUIRE(rb.available() == 32);

        rb.reset();
        REQUIRE(rb.available() == 0);
    }

    SECTION("Available") {
        REQUIRE(rb.available() == 0);

        std::vector<uint8_t> data(50, 0x01);
        rb.write(data.data(), data.size());
        REQUIRE(rb.available() == 50);

        uint8_t buf[20];
        rb.read(buf, 20);
        REQUIRE(rb.available() == 30);
    }

    SECTION("MultipleWriteRead") {
        for (int round = 0; round < 10; ++round) {
            std::vector<uint8_t> data(20);
            std::fill(data.begin(), data.end(), static_cast<uint8_t>(round));

            REQUIRE(rb.write(data.data(), data.size()) == 20);
            std::vector<uint8_t> out(20);
            REQUIRE(rb.read(out.data(), out.size()) == 20);
            REQUIRE(out == data);
        }
        REQUIRE(rb.available() == 0);
    }
}
