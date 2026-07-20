// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// PC1 (IFF/ILBM) full-screen image tests — synthetic hand-authored
// containers only.

#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "formats/pc1.hpp"

using olduvai::formats::Pc1Error;
using olduvai::formats::Pc1Image;
using olduvai::formats::parse_pc1;

namespace {

void u32be(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 3; i >= 0; --i)
        v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
}

void u16be(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
}

void tag(std::vector<std::uint8_t>& v, const char* t) {
    v.insert(v.end(), t, t + 4);
}

std::vector<std::uint8_t> bmhd(std::uint16_t w, std::uint16_t h,
                               std::uint8_t nplanes, std::uint8_t comp) {
    std::vector<std::uint8_t> c;
    u16be(c, w); u16be(c, h);
    u16be(c, 0); u16be(c, 0);          // x, y
    c.push_back(nplanes);
    c.push_back(0);                    // masking
    c.push_back(comp);
    c.push_back(0);                    // pad
    u16be(c, 0);                       // transparentColor
    c.push_back(1); c.push_back(1);    // aspect
    u16be(c, w); u16be(c, h);          // page size
    return c;
}

std::vector<std::uint8_t> cmap16() {
    std::vector<std::uint8_t> c;
    for (int i = 0; i < 16; ++i) {
        c.push_back(static_cast<std::uint8_t>(i * 0x10));  // Atari-style 0x10 steps
        c.push_back(0x00);
        c.push_back(0xF0);
    }
    return c;
}

std::vector<std::uint8_t> iff(const std::vector<std::pair<std::string,
                              std::vector<std::uint8_t>>>& chunks) {
    std::vector<std::uint8_t> inner;
    tag(inner, "ILBM");
    for (const auto& [t, body] : chunks) {
        tag(inner, t.c_str());
        u32be(inner, static_cast<std::uint32_t>(body.size()));
        inner.insert(inner.end(), body.begin(), body.end());
        if (body.size() % 2) inner.push_back(0);  // IFF even padding
    }
    std::vector<std::uint8_t> out;
    tag(out, "FORM");
    u32be(out, static_cast<std::uint32_t>(inner.size()));
    out.insert(out.end(), inner.begin(), inner.end());
    return out;
}

}  // namespace

TEST_CASE("not IFF / not ILBM throws") {
    CHECK_THROWS_AS(parse_pc1({'X', 'X', 'X', 'X', 0, 0, 0, 0}), Pc1Error);
    std::vector<std::uint8_t> v;
    tag(v, "FORM"); u32be(v, 4); tag(v, "AIFF");
    CHECK_THROWS_AS(parse_pc1(v), Pc1Error);
}

TEST_CASE("uncompressed 8x2, 4 interleaved planes decode") {
    // Row layout: plane0..plane3, 1 byte each (groups=1).
    // Row 0: px0 idx = 0b1111 = 15; px7 idx = 0b0001 = 1.
    // Row 1: px0 idx = 0b0100 = 4.
    std::vector<std::uint8_t> body = {
        0b10000001, 0b10000000, 0b10000000, 0b10000000,  // row 0 planes 0-3
        0b00000000, 0b00000000, 0b10000000, 0b00000000,  // row 1 planes 0-3
    };
    const auto img = parse_pc1(iff({{"BMHD", bmhd(8, 2, 4, 0)},
                                    {"CMAP", cmap16()},
                                    {"BODY", body}}));
    CHECK(img.width == 8);
    CHECK(img.height == 2);
    CHECK(img.pixels[0] == 15);
    CHECK(img.pixels[7] == 1);
    CHECK(img.pixels[8 + 0] == 4);
    CHECK(img.pixels[8 + 1] == 0);
}

TEST_CASE("packbits BODY decompresses (compression=1)") {
    // 8x1, 4 planes → 4 raw bytes, all 0xFF → every pixel idx 15.
    // PackBits: repeat 0xFF ×4 (control 0xFD = 257-253 = 4).
    const auto img = parse_pc1(iff({{"BMHD", bmhd(8, 1, 4, 1)},
                                    {"CMAP", cmap16()},
                                    {"BODY", {0xFD, 0xFF}}}));
    for (int x = 0; x < 8; ++x) CHECK(img.pixels[x] == 15);
}

TEST_CASE("CMAP converts via 6-bit DAC truncation") {
    // 0xF0 >> 2 = 0x3C = 60 → round(60*255/63) = 243.
    // 0x10 >> 2 = 4 → round(4*255/63) = 16.
    const auto img = parse_pc1(iff({{"BMHD", bmhd(8, 1, 4, 0)},
                                    {"CMAP", cmap16()},
                                    {"BODY", {0, 0, 0, 0}}}));
    REQUIRE(img.palette.size() == 16);
    CHECK(img.palette[1].r == 16);
    CHECK(img.palette[0].b == 243);
    CHECK(img.palette[0].g == 0);
}

TEST_CASE("odd-size chunks are even-padded (DPPV before BODY)") {
    const auto img = parse_pc1(iff({{"BMHD", bmhd(8, 1, 4, 0)},
                                    {"DPPV", {1, 2, 3}},        // odd → padded
                                    {"CMAP", cmap16()},
                                    {"BODY", {0, 0, 0, 0}}}));
    CHECK(img.width == 8);
}

TEST_CASE("missing CMAP or BODY throws") {
    CHECK_THROWS_AS(parse_pc1(iff({{"BMHD", bmhd(8, 1, 4, 0)},
                                   {"BODY", {0, 0, 0, 0}}})), Pc1Error);
    CHECK_THROWS_AS(parse_pc1(iff({{"BMHD", bmhd(8, 1, 4, 0)},
                                   {"CMAP", cmap16()}})), Pc1Error);
}

TEST_CASE("truncated CMAP/BMHD clamp to present bytes (no OOB read)") {
    // CMAP whose header declares more bytes than the file holds: the parser
    // must iterate only over present bytes (review 2026-07-03 S2).  Build a
    // valid file, then cut it 6 bytes into the CMAP payload — 2 complete
    // palette entries survive; BODY is gone so the parse throws cleanly.
    auto data = iff({{"BMHD", bmhd(8, 2, 4, 0)}, {"CMAP", cmap16()}});
    // FORM(8) + ILBM(4) + BMHD hdr(8)+20 + CMAP hdr(8) + 6 payload bytes
    data.resize(8 + 4 + 8 + 20 + 8 + 6);
    CHECK_THROWS_AS(parse_pc1(data), Pc1Error);   // missing BODY — but no UB
    // File ending inside the BMHD payload itself.
    auto data2 = iff({{"BMHD", bmhd(8, 2, 4, 0)}});
    data2.resize(8 + 4 + 8 + 5);
    CHECK_THROWS_AS(parse_pc1(data2), Pc1Error);
}
