// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// LZSS decoder unit tests — synthetic hand-authored bitstreams only
// (parity with the reference implementation's validated synthetic suite).

#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "formats/lzss.hpp"

using olduvai::formats::lzss_decompress;
using olduvai::formats::LzssError;

namespace {

// Pack a '0'/'1' string into bytes, MSB-first, zero-padded to a byte.
std::vector<std::uint8_t> build_bitstream(std::string bits) {
    while (bits.size() % 8 != 0) bits.push_back('0');
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < bits.size(); i += 8) {
        std::uint8_t b = 0;
        for (std::size_t j = 0; j < 8; ++j) {
            b = static_cast<std::uint8_t>((b << 1) | (bits[i + j] == '1'));
        }
        out.push_back(b);
    }
    return out;
}

std::string bin(unsigned value, int width) {
    std::string s;
    for (int i = width - 1; i >= 0; --i) s.push_back(((value >> i) & 1) ? '1' : '0');
    return s;
}

// Complete LZSS data: UINT32BE size header + packed bitstream.
std::vector<std::uint8_t> make_lzss(std::uint32_t expected,
                                    const std::string& bits) {
    std::vector<std::uint8_t> out = {
        static_cast<std::uint8_t>(expected >> 24),
        static_cast<std::uint8_t>(expected >> 16),
        static_cast<std::uint8_t>(expected >> 8),
        static_cast<std::uint8_t>(expected),
    };
    const auto payload = build_bitstream(bits);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::string literal(char ch) {
    return "0" + bin(static_cast<unsigned char>(ch), 8);
}

std::string backref(unsigned length_code, unsigned distance_code) {
    return "1" + bin(length_code, 2) + bin(distance_code, 8);
}

std::vector<std::uint8_t> bytes_of(const std::string& s) {
    return {s.begin(), s.end()};
}

}  // namespace

TEST_CASE("all literals") {
    std::string bits;
    for (char ch : std::string("ABC")) bits += literal(ch);
    CHECK(lzss_decompress(make_lzss(3, bits)) == bytes_of("ABC"));
}

TEST_CASE("simple backreference copies earlier output") {
    std::string bits;
    for (char ch : std::string("ABC")) bits += literal(ch);
    bits += backref(1, 2);  // length 3, distance 3
    CHECK(lzss_decompress(make_lzss(6, bits)) == bytes_of("ABCABC"));
}

TEST_CASE("backreference into zero-prefilled window yields null bytes") {
    std::string bits = backref(2, 0);  // length 4, distance 1
    bits += literal('\xFF');
    const auto out = lzss_decompress(make_lzss(5, bits));
    CHECK(out == std::vector<std::uint8_t>{0, 0, 0, 0, 0xFF});
}

TEST_CASE("max length code copies five bytes") {
    std::string bits;
    for (char ch : std::string("HELLO")) bits += literal(ch);
    bits += backref(3, 4);  // length 5, distance 5
    CHECK(lzss_decompress(make_lzss(10, bits)) == bytes_of("HELLOHELLO"));
}

TEST_CASE("overlapping backreference run-length expands") {
    std::string bits = literal('A');
    bits += backref(2, 0);  // length 4, distance 1 → AAAA appended
    CHECK(lzss_decompress(make_lzss(5, bits)) == bytes_of("AAAAA"));
}

TEST_CASE("backreference stops early at declared output size") {
    // Declared size 4 but the backref would copy 5 — must stop at 4.
    std::string bits = literal('A');
    bits += backref(3, 0);  // length 5, distance 1
    CHECK(lzss_decompress(make_lzss(4, bits)) == bytes_of("AAAA"));
}

TEST_CASE("header too short throws") {
    const std::vector<std::uint8_t> data = {0x00, 0x00};
    CHECK_THROWS_AS(lzss_decompress(data), LzssError);
}

TEST_CASE("zero-length expected output") {
    CHECK(lzss_decompress(make_lzss(0, "")).empty());
}

TEST_CASE("implausible declared output size throws instead of allocating") {
    // The 32-bit size header is corruption-controlled and drives reserve():
    // 0xFFFFFFFF meant a 4 GiB allocation attempt (review 2026-07-03 S5).
    const std::uint8_t hdr[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    CHECK_THROWS_AS(lzss_decompress(hdr, 4), LzssError);
}
