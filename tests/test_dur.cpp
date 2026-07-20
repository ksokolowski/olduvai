// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// DUR tile-collision table tests — synthetic 960-byte images only.

#include <cstdint>
#include <vector>

#include "doctest/doctest.h"
#include "formats/dur.hpp"

using olduvai::formats::DurError;
using olduvai::formats::parse_dur;

namespace {

void i16le(std::vector<std::uint8_t>& v, std::int16_t x) {
    const auto u = static_cast<std::uint16_t>(x);
    v.push_back(static_cast<std::uint8_t>(u & 0xFF));
    v.push_back(static_cast<std::uint8_t>(u >> 8));
}

// 960-byte image, all entries sentinel-terminated immediately.
std::vector<std::uint8_t> empty_image() {
    std::vector<std::uint8_t> out;
    for (int e = 0; e < 40; ++e) {
        for (int s = 0; s < 4; ++s) {
            i16le(out, -1); i16le(out, 0); i16le(out, 0);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("wrong size throws") {
    CHECK_THROWS_AS(parse_dur(std::vector<std::uint8_t>(959, 0)), DurError);
    CHECK_THROWS_AS(parse_dur(std::vector<std::uint8_t>(961, 0)), DurError);
}

TEST_CASE("empty table parses 40 tiles with no segments") {
    const auto level = parse_dur(empty_image());
    REQUIRE(level.tiles.size() == 40);
    for (const auto& t : level.tiles) CHECK(t.segments.empty());
}

TEST_CASE("segments parse until the dx == -1 sentinel") {
    auto img = empty_image();
    // tile 3: two segments then sentinel.  Entry offset = 3*24.
    std::vector<std::uint8_t> entry;
    i16le(entry, 0);  i16le(entry, 8);  i16le(entry, 48);
    i16le(entry, -4); i16le(entry, 15); i16le(entry, 24);
    i16le(entry, -1); i16le(entry, 0);  i16le(entry, 0);
    i16le(entry, 7);  i16le(entry, 7);  i16le(entry, 7);  // after sentinel: ignored
    std::copy(entry.begin(), entry.end(), img.begin() + 3 * 24);
    const auto level = parse_dur(img);
    const auto& segs = level.tiles[3].segments;
    REQUIRE(segs.size() == 2);
    CHECK(segs[0].dx == 0);  CHECK(segs[0].dy == 8);  CHECK(segs[0].width == 48);
    CHECK(segs[1].dx == -4); CHECK(segs[1].dy == 15); CHECK(segs[1].width == 24);
}

TEST_CASE("a full entry carries four segments") {
    auto img = empty_image();
    std::vector<std::uint8_t> entry;
    for (int s = 0; s < 4; ++s) {
        i16le(entry, static_cast<std::int16_t>(s));
        i16le(entry, static_cast<std::int16_t>(s * 2));
        i16le(entry, static_cast<std::int16_t>(s * 3 + 1));
    }
    std::copy(entry.begin(), entry.end(), img.begin() + 7 * 24);
    const auto level = parse_dur(img);
    CHECK(level.tiles[7].segments.size() == 4);
    CHECK(level.tiles[7].segments[3].width == 10);
}

TEST_CASE("all-zero triple terminates the segment list") {
    // (0,0,0) is a width-0 no-op in the original collision writer; the
    // parser treats it as end-of-list (identical resulting bitmaps).
    auto img = empty_image();
    std::vector<std::uint8_t> entry;
    i16le(entry, 0); i16le(entry, 0); i16le(entry, 0);
    i16le(entry, 5); i16le(entry, 5); i16le(entry, 5);
    std::copy(entry.begin(), entry.end(), img.begin() + 0 * 24);
    const auto level = parse_dur(img);
    CHECK(level.tiles[0].segments.empty());
}
