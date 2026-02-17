#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// Lock-free single-producer single-consumer ring buffer.
// Producer (PipeWire thread) calls write(). Consumer (main thread) calls read().
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity_bytes)
        : buf_(capacity_bytes), capacity_(capacity_bytes) {}

    // Producer: write data into ring buffer. Returns bytes actually written.
    size_t write(const void* data, size_t len) {
        size_t w = write_pos_.load(std::memory_order_relaxed);
        size_t r = read_pos_.load(std::memory_order_acquire);

        size_t avail = capacity_ - (w - r);
        size_t to_write = std::min(len, avail);
        if (to_write == 0) return 0;

        auto src = static_cast<const uint8_t*>(data);
        size_t offset = w % capacity_;
        size_t first = std::min(to_write, capacity_ - offset);
        std::memcpy(buf_.data() + offset, src, first);
        if (first < to_write) {
            std::memcpy(buf_.data(), src + first, to_write - first);
        }

        write_pos_.store(w + to_write, std::memory_order_release);
        return to_write;
    }

    // Consumer: read up to max_len bytes. Returns bytes actually read.
    size_t read(void* dest, size_t max_len) {
        size_t r = read_pos_.load(std::memory_order_relaxed);
        size_t w = write_pos_.load(std::memory_order_acquire);

        size_t avail = w - r;
        size_t to_read = std::min(max_len, avail);
        if (to_read == 0) return 0;

        auto dst = static_cast<uint8_t*>(dest);
        size_t offset = r % capacity_;
        size_t first = std::min(to_read, capacity_ - offset);
        std::memcpy(dst, buf_.data() + offset, first);
        if (first < to_read) {
            std::memcpy(dst + first, buf_.data(), to_read - first);
        }

        read_pos_.store(r + to_read, std::memory_order_release);
        return to_read;
    }

    // Consumer: drain all available data into a vector of int16_t samples.
    std::vector<int16_t> drain_all() {
        size_t r = read_pos_.load(std::memory_order_relaxed);
        size_t w = write_pos_.load(std::memory_order_acquire);
        size_t avail = w - r;

        // Align to sample boundary (2 bytes per int16_t)
        avail &= ~size_t(1);
        if (avail == 0) return {};

        std::vector<int16_t> samples(avail / sizeof(int16_t));
        read(samples.data(), avail);
        return samples;
    }

    size_t available() const {
        size_t w = write_pos_.load(std::memory_order_acquire);
        size_t r = read_pos_.load(std::memory_order_acquire);
        return w - r;
    }

    void reset() {
        read_pos_.store(0, std::memory_order_relaxed);
        write_pos_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<uint8_t> buf_;
    size_t capacity_;
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};
};
