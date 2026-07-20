// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// MAT sprite container tests — synthetic hand-authored files only.

#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "formats/mat.hpp"

using olduvai::formats::MatFile;
using olduvai::formats::Sprite;
using olduvai::formats::SpriteFormat;

namespace {

void u16be(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
}

}  // namespace

TEST_CASE("monochrome file (font container) parses 1bpp glyphs") {
    // count=1; one 8x2 glyph, isize=0, 1bpp → 2 raw bytes.
    std::vector<std::uint8_t> data;
    u16be(data, 1);
    u16be(data, 8); u16be(data, 2); u16be(data, 0);
    data.push_back(0b10000001);
    data.push_back(0b01111110);
    MatFile mat(data, "CHARSET1.MAT");
    REQUIRE(mat.sprites().size() == 1);
    const Sprite& s = mat.sprites()[0];
    CHECK(s.format == SpriteFormat::Mono1bpp);
    CHECK(s.width == 8);
    CHECK(s.height == 2);
    CHECK(s.raw_pixels.size() == 2);
    // bit 7 = leftmost pixel
    const auto px = s.decode_indexed();
    CHECK(px[0].opaque);          // x=0,y=0 set
    CHECK_FALSE(px[1].opaque);    // x=1,y=0 clear
    CHECK(px[8 + 0].opaque == false);  // x=0,y=1 clear
    CHECK(px[8 + 1].opaque);           // x=1,y=1 set
}

TEST_CASE("uncompressed 5-plane sprite decodes colour index + mask") {
    // count=1; 8x1 sprite, isize=0 → 5 planes × 1 byte each.
    // pixel 0: colour 0b1010 = 10, opaque.  pixel 1: colour 5, opaque.
    // pixel 2: transparent (mask 0) regardless of plane bits.
    std::vector<std::uint8_t> data;
    u16be(data, 1);
    u16be(data, 8); u16be(data, 1); u16be(data, 0);
    data.push_back(0b01000000);  // plane0 (bit0): px1
    data.push_back(0b10100000);  // plane1 (bit1): px0, px2
    data.push_back(0b01000000);  // plane2 (bit2): px1
    data.push_back(0b10000000);  // plane3 (bit3): px0
    data.push_back(0b11000000);  // mask: px0, px1 opaque; px2 transparent
    MatFile mat(data, "L1SPR.MAT");
    REQUIRE(mat.sprites().size() == 1);
    const Sprite& s = mat.sprites()[0];
    CHECK(s.format == SpriteFormat::Ilbm5);
    const auto px = s.decode_indexed();
    REQUIRE(px.size() == 8);
    CHECK(px[0].opaque); CHECK(px[0].color == 0b1010);
    CHECK(px[1].opaque); CHECK(px[1].color == 0b0101);
    CHECK_FALSE(px[2].opaque);
    CHECK_FALSE(px[7].opaque);
}

TEST_CASE("packbits-compressed sprite inflates to 5-plane size") {
    // 8x1 sprite → expected raw = 5 bytes.  Compressed: repeat 0xFF ×5
    // (control 0xFC = 257-252 = 5).
    std::vector<std::uint8_t> data;
    u16be(data, 1);
    u16be(data, 8); u16be(data, 1); u16be(data, 2);  // isize=2
    data.push_back(0xFC); data.push_back(0xFF);
    MatFile mat(data, "L1SPR.MAT");
    REQUIRE(mat.sprites().size() == 1);
    const Sprite& s = mat.sprites()[0];
    CHECK(s.raw_pixels == std::vector<std::uint8_t>(5, 0xFF));
    const auto px = s.decode_indexed();
    CHECK(px[0].opaque); CHECK(px[0].color == 15);
}

TEST_CASE("multiple sprites parse sequentially") {
    std::vector<std::uint8_t> data;
    u16be(data, 2);
    u16be(data, 8); u16be(data, 1); u16be(data, 0);
    data.insert(data.end(), 5, 0x00);
    u16be(data, 16); u16be(data, 2); u16be(data, 0);
    data.insert(data.end(), 2 * 5 * 2, 0xAA);
    MatFile mat(data, "L3SPR.MAT");
    REQUIRE(mat.sprites().size() == 2);
    CHECK(mat.sprites()[1].width == 16);
    CHECK(mat.sprites()[1].raw_pixels.size() == 2 * 5 * 2);
}

TEST_CASE("zero-dimension entry stops the parse") {
    std::vector<std::uint8_t> data;
    u16be(data, 2);
    u16be(data, 0); u16be(data, 4); u16be(data, 0);  // width 0 → stop
    MatFile mat(data, "L1SPR.MAT");
    CHECK(mat.sprites().empty());
}

TEST_CASE("truncated header stops the parse gracefully") {
    std::vector<std::uint8_t> data;
    u16be(data, 3);
    u16be(data, 8); u16be(data, 1); u16be(data, 0);
    data.insert(data.end(), 5, 0x00);
    u16be(data, 8);  // header cut mid-entry
    MatFile mat(data, "L1SPR.MAT");
    CHECK(mat.sprites().size() == 1);
}
