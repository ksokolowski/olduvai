// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include <doctest/doctest.h>

#include "formats/unsqz.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

using olduvai::formats::looks_like_sqz;
using olduvai::formats::SqzError;
using olduvai::formats::unsqz;

namespace {

// Minimal SQZ/LZW encoder — test-only reference for round-trips.  Mirrors
// the decoder's semantics: 9→12-bit codes MSB-first, dictionary frozen at
// 0x1000 entries, 0x101 = full reset.  Width-growth timing: the decoder
// checks its next-free counter BEFORE reading each code, and that counter
// runs one entry BEHIND the encoder's discovery order (the decoder learns
// an entry only from the code after it), so the encoder tracks the
// decoder's counter (`dec_free_`) separately from its own assignments.
class SqzEncoder {
public:
    void add_bytes(const std::vector<std::uint8_t>& data) {
        total_ += data.size();
        for (const std::uint8_t k : data) {
            if (!have_w_) {
                w_ = k;
                have_w_ = true;
                continue;
            }
            const auto it = dict_.find({w_, k});
            if (it != dict_.end()) {
                w_ = it->second;
            } else {
                emit(w_);
                if (assign_next_ < 0x1000) {
                    dict_[{w_, k}] = assign_next_;
                    ++assign_next_;
                }
                w_ = k;
            }
        }
    }

    // Emit a mid-stream full reset (code 0x101), like the periodic resets
    // in real containers.
    void reset() {
        flush_w();
        emit(0x101);
        dict_.clear();
        width_ = 9;
        top_ = 1u << 9;
        dec_free_ = 0x102;
        assign_next_ = 0x102;
        first_ = true;
    }

    std::vector<std::uint8_t> finish() {
        flush_w();
        while (nbits_ % 8 != 0) push_bit(0);   // pad final byte
        std::vector<std::uint8_t> out(4);
        out[0] = static_cast<std::uint8_t>((total_ >> 16) & 0x0F);
        out[1] = 0x10;
        out[2] = static_cast<std::uint8_t>(total_ & 0xFF);
        out[3] = static_cast<std::uint8_t>((total_ >> 8) & 0xFF);
        out.insert(out.end(), bytes_.begin(), bytes_.end());
        return out;
    }

private:
    void flush_w() {
        if (have_w_) {
            emit(w_);
            have_w_ = false;
        }
    }

    void emit(std::uint16_t code) {
        // The decoder checks growth BEFORE reading every code.
        if (dec_free_ == top_ && width_ != 12) {
            ++width_;
            top_ = static_cast<std::uint16_t>(top_ << 1);
        }
        for (int b = width_ - 1; b >= 0; --b) push_bit((code >> b) & 1);
        if (code == 0x101) return;         // reset: decoder adds no entry
        if (first_) {
            first_ = false;                // post-reset literal: no entry
        } else if (dec_free_ < 0x1000) {
            ++dec_free_;                   // decoder adds one entry per code
        }
    }

    void push_bit(int bit) {
        if (nbits_ % 8 == 0) bytes_.push_back(0);
        if (bit != 0) {
            bytes_.back() = static_cast<std::uint8_t>(
                bytes_.back() | (0x80u >> (nbits_ % 8)));
        }
        ++nbits_;
    }

    std::map<std::pair<std::uint16_t, std::uint8_t>, std::uint16_t> dict_;
    std::vector<std::uint8_t> bytes_;
    std::size_t nbits_ = 0;
    std::size_t total_ = 0;
    int width_ = 9;
    std::uint16_t top_ = 1u << 9;
    std::uint16_t dec_free_ = 0x102;       // decoder's next-free counter
    std::uint16_t assign_next_ = 0x102;    // encoder's dictionary assignment
    std::uint16_t w_ = 0;
    bool have_w_ = false;
    bool first_ = true;
};

std::vector<std::uint8_t> encode(const std::vector<std::uint8_t>& data) {
    SqzEncoder enc;
    enc.add_bytes(data);
    return enc.finish();
}

// Deterministic pseudo-random bytes (no <random> to keep runs identical).
std::vector<std::uint8_t> noise(std::size_t n, std::uint32_t seed) {
    std::vector<std::uint8_t> out(n);
    std::uint32_t s = seed;
    for (auto& b : out) {
        s = s * 1103515245u + 12345u;
        b = static_cast<std::uint8_t>(s >> 16);
    }
    return out;
}

}  // namespace

TEST_CASE("looks_like_sqz: header probe") {
    CHECK(!looks_like_sqz(std::vector<std::uint8_t>{}));
    CHECK(!looks_like_sqz({0x02, 0x10, 0x74}));                    // short
    CHECK(!looks_like_sqz({0x02, 0x20, 0x74, 0x7F, 0x00}));       // bad tag
    CHECK(!looks_like_sqz({0x00, 0x10, 0x00, 0x00, 0x00}));       // size 0
    CHECK(looks_like_sqz({0x02, 0x10, 0x74, 0x7F, 0x00}));
    // An MZ executable must never probe as SQZ.
    CHECK(!looks_like_sqz({'M', 'Z', 0x46, 0x01, 0x41}));
}

TEST_CASE("unsqz: literal round-trip") {
    const std::vector<std::uint8_t> data = {'H', 'E', 'L', 'L', 'O'};
    CHECK(unsqz(encode(data)) == data);
}

TEST_CASE("unsqz: repetitive data round-trip (dictionary chains + KwKwK)") {
    std::vector<std::uint8_t> data;
    for (int i = 0; i < 500; ++i) {
        data.push_back('A');
        data.push_back('B');
    }
    CHECK(unsqz(encode(data)) == data);
}

TEST_CASE("unsqz: single-byte run (immediate KwKwK case)") {
    const std::vector<std::uint8_t> data(1000, 'X');
    CHECK(unsqz(encode(data)) == data);
}

TEST_CASE("unsqz: width growth to 12 bits and frozen dictionary") {
    // Enough distinct pairs to cross every width step and fill the
    // dictionary (frozen-dictionary decoding afterwards).
    const auto data = noise(64 * 1024, 0xC0FFEE);
    CHECK(unsqz(encode(data)) == data);
}

TEST_CASE("unsqz: mid-stream reset (code 0x101) continues the stream") {
    SqzEncoder enc;
    const auto a = noise(8 * 1024, 1);
    const auto b = noise(8 * 1024, 2);
    enc.add_bytes(a);
    enc.reset();
    enc.add_bytes(b);
    const auto packed = enc.finish();
    auto expect = a;
    expect.insert(expect.end(), b.begin(), b.end());
    CHECK(unsqz(packed) == expect);
}

TEST_CASE("unsqz: malformed input throws") {
    CHECK_THROWS_AS(unsqz(std::vector<std::uint8_t>{0x00, 0x20, 0x01, 0x00}),
                    SqzError);
    // Declared size larger than the stream delivers → truncation.
    auto packed = encode({'A', 'B', 'C'});
    packed[2] = 0xFF;   // inflate declared size
    packed[3] = 0x00;
    CHECK_THROWS_AS(unsqz(packed), SqzError);
}
