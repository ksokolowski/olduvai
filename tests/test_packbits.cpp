// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// PackBits (IFF/ILBM variant) decoder tests — synthetic streams only.

#include <cstdint>
#include <vector>

#include "doctest/doctest.h"
#include "formats/packbits.hpp"

using olduvai::formats::packbits_decompress;

namespace {
std::vector<std::uint8_t> B(std::initializer_list<int> v) {
    std::vector<std::uint8_t> out;
    for (int x : v) out.push_back(static_cast<std::uint8_t>(x));
    return out;
}
}  // namespace

TEST_CASE("literal run copies n+1 bytes") {
    // control 0x02 → copy 3 literal bytes
    CHECK(packbits_decompress(B({0x02, 'A', 'B', 'C'}), 3) == B({'A', 'B', 'C'}));
}

TEST_CASE("repeat run emits 257-n copies") {
    // control 0xFE → repeat next byte 257-254=3 times
    CHECK(packbits_decompress(B({0xFE, 'X'}), 3) == B({'X', 'X', 'X'}));
}

TEST_CASE("0x80 is a no-op") {
    CHECK(packbits_decompress(B({0x80, 0x00, 'Q'}), 1) == B({'Q'}));
}

TEST_CASE("mixed runs") {
    // 2 literals, no-op, repeat 'z' ×4 (0xFD = 257-253 = 4)
    const auto out = packbits_decompress(B({0x01, 'h', 'i', 0x80, 0xFD, 'z'}), 6);
    CHECK(out == B({'h', 'i', 'z', 'z', 'z', 'z'}));
}

TEST_CASE("output truncated to expected size") {
    // repeat ×5 but only 2 expected
    CHECK(packbits_decompress(B({0xFC, 'A'}), 2) == B({'A', 'A'}));
}

TEST_CASE("input exhaustion stops early (short output)") {
    CHECK(packbits_decompress(B({0x02, 'A'}), 10) == B({'A'}));
}

TEST_CASE("empty input, zero expected") {
    CHECK(packbits_decompress({}, 0).empty());
}
