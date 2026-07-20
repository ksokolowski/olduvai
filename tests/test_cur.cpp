// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// CUR/VGA archive reader tests — synthetic hand-authored archive images
// only (no game bytes).

#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "formats/cur.hpp"

using olduvai::formats::CurArchive;
using olduvai::formats::CurError;
using olduvai::formats::is_compressed_name;

namespace {

void put_u16le(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFF));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}

void put_u32le(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFF));
    }
}

void put_name(std::vector<std::uint8_t>& v, const std::string& name) {
    v.insert(v.end(), name.begin(), name.end());
    v.push_back(0);
}

// Build an archive image: {u16le data-offset}{FAT}{terminator?}{data...}.
std::vector<std::uint8_t> build_archive(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files,
    bool zero_terminator = true) {
    std::vector<std::uint8_t> fat;
    for (const auto& [name, data] : files) {
        put_u32le(fat, static_cast<std::uint32_t>(data.size()));
        put_name(fat, name);
    }
    if (zero_terminator) put_u32le(fat, 0);

    std::vector<std::uint8_t> out;
    put_u16le(out, static_cast<std::uint16_t>(2 + fat.size()));
    out.insert(out.end(), fat.begin(), fat.end());
    for (const auto& [name, data] : files) {
        out.insert(out.end(), data.begin(), data.end());
    }
    return out;
}

// Minimal valid LZSS payload decoding to "ABC" (3 literals).
std::vector<std::uint8_t> lzss_abc() {
    // Header: size 3 (u32be).  Bits: (0 'A')(0 'B')(0 'C'), MSB-first,
    // zero-padded to whole bytes.
    return {0, 0, 0, 3, 0x20, 0x90, 0x88, 0x60};
}

}  // namespace

TEST_CASE("compressed-name rule") {
    CHECK(is_compressed_name("L1SPR.MAT"));
    CHECK(is_compressed_name("fond1.pc1"));
    CHECK(is_compressed_name("MUSIC1.MDI"));
    CHECK_FALSE(is_compressed_name("CHARSET1.MAT"));  // the raw exception
    CHECK_FALSE(is_compressed_name("LEVEL1.DUR"));
    CHECK_FALSE(is_compressed_name("MASSUE2.VOC"));
    CHECK_FALSE(is_compressed_name("NOEXT"));
}

TEST_CASE("raw entries round-trip with case-insensitive lookup") {
    const auto img = build_archive({
        {"LEVEL1.DUR", {1, 2, 3, 4}},
        {"B.VOC", {9, 9}},
    });
    CurArchive ar(img);
    CHECK(ar.entries().size() == 2);
    CHECK(ar.contains("level1.dur"));
    CHECK(ar.get("Level1.DUR").data == std::vector<std::uint8_t>{1, 2, 3, 4});
    CHECK(ar.get("B.VOC").stored_data == std::vector<std::uint8_t>{9, 9});
    CHECK_FALSE(ar.get("B.VOC").compressed);
}

TEST_CASE("compressed entry is decoded; stored bytes preserved") {
    const auto payload = lzss_abc();
    const auto img = build_archive({{"X.PC1", payload}});
    CurArchive ar(img);
    const auto& e = ar.get("X.PC1");
    CHECK(e.compressed);
    CHECK(e.data == std::vector<std::uint8_t>{'A', 'B', 'C'});
    CHECK(e.stored_data == payload);
}

TEST_CASE("FAT without zero terminator parses to data offset") {
    const auto img = build_archive({{"A.DUR", {7}}}, /*zero_terminator=*/false);
    CurArchive ar(img);
    CHECK(ar.entries().size() == 1);
    CHECK(ar.get("A.DUR").data == std::vector<std::uint8_t>{7});
}

TEST_CASE("missing entry throws") {
    const auto img = build_archive({{"A.DUR", {7}}});
    CurArchive ar(img);
    CHECK_THROWS_AS(ar.get("NOPE.PC1"), CurError);
}

TEST_CASE("truncated data throws") {
    auto img = build_archive({{"A.DUR", {1, 2, 3, 4}}});
    img.resize(img.size() - 2);  // chop the tail of the data section
    CHECK_THROWS_AS(CurArchive{img}, CurError);
}

TEST_CASE("archive too short throws") {
    const std::vector<std::uint8_t> img = {0x05};
    CHECK_THROWS_AS(CurArchive{img}, CurError);
}
