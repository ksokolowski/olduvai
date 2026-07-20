// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Executable-embedded table readers — synthetic in-memory images only
// (values poked at the computed file offsets of a zero-filled buffer).

#include <cstdint>
#include <vector>

#include "doctest/doctest.h"
#include "prepare/exe_tables.hpp"

using namespace olduvai::prepare;

namespace {

std::vector<std::uint8_t> blank_exe() {
    // Large enough to cover every table offset we poke in tests.
    return std::vector<std::uint8_t>(ds_offset_to_file(0x8000), 0);
}

void poke16(std::vector<std::uint8_t>& exe, std::size_t file_off,
            std::uint16_t v) {
    exe[file_off] = static_cast<std::uint8_t>(v & 0xFF);
    exe[file_off + 1] = static_cast<std::uint8_t>(v >> 8);
}

}  // namespace

TEST_CASE("ds_offset_to_file mapping") {
    // 0x2000 + (0x2D99 - 0x1000) * 16 = 0x2000 + 0x1D990 = 0x1F990.
    CHECK(ds_offset_to_file(0) == 0x1F990);
    CHECK(ds_offset_to_file(0x87AC) == 0x1F990 + 0x87AC);
}

TEST_CASE("detect_exe_layout: canonical defaults and MZ header size") {
    // Non-MZ / unknown image → canonical layout (old behaviour preserved).
    const auto blank = blank_exe();
    const ExeLayout def = detect_exe_layout(blank);
    CHECK(def.header_size == 0x2000);
    CHECK(def.ds_delta == 0);

    // MZ header: e_cparhdr (paragraphs) at offset 8 drives header_size.
    std::vector<std::uint8_t> mz(0x20, 0);
    mz[0] = 'M';
    mz[1] = 'Z';
    mz[8] = 0xE1;   // 481 paragraphs → 7696-byte header (the CD re-link)
    mz[9] = 0x01;
    const ExeLayout cd = detect_exe_layout(mz);
    CHECK(cd.header_size == 481 * 16);
    CHECK(cd.ds_delta == 0);   // unknown digest → no offset shift

    // Layout-aware mapping applies both fields.
    ExeLayout l;
    l.header_size = 481 * 16;
    l.ds_delta = 30;
    CHECK(ds_offset_to_file(0x2076, l) == 481 * 16 + 0x1D990 + 0x2076 + 30);
}

TEST_CASE("tile table: counts are stored as count-1; records decode") {
    auto exe = blank_exe();
    // L1: counts at DS:0x2076, records at DS:0x209E.
    const std::size_t counts = ds_offset_to_file(0x2076);
    const std::size_t recs = ds_offset_to_file(0x209E);
    poke16(exe, counts, 1);  // screen 0: 2 tiles
    // (remaining 18 screens read raw 0 → 1 tile each, records all zero)
    // tile A: x_raw = 0x102 (x=256, layer=2), y = -8, sprite 1-based 29
    poke16(exe, recs + 0, 0x0102);
    poke16(exe, recs + 2, static_cast<std::uint16_t>(-8));
    poke16(exe, recs + 4, 29);
    // tile B: x_raw = 0x0030 (x=48, layer=0), y = 100, sprite 1
    poke16(exe, recs + 6, 0x0030);
    poke16(exe, recs + 8, 100);
    poke16(exe, recs + 10, 1);

    const auto tiles = read_tile_table(exe, 1);
    CHECK(tiles.level == 1);
    CHECK(tiles.screens.size() == 19);  // playable cap
    REQUIRE(tiles.screens[0].tiles.size() == 2);
    const auto& a = tiles.screens[0].tiles[0];
    CHECK(a.x == 256); CHECK(a.layer == 2); CHECK(a.y == -8);
    CHECK(a.sprite_idx == 28);  // 1-based → 0-based
    const auto& b = tiles.screens[0].tiles[1];
    CHECK(b.x == 48); CHECK(b.layer == 0); CHECK(b.sprite_idx == 0);
}

TEST_CASE("unknown tile level throws") {
    const auto exe = blank_exe();
    CHECK_THROWS_AS(read_tile_table(exe, 2), ExeTableError);
}

TEST_CASE("object table: separators split screens, sentinel ends") {
    auto exe = blank_exe();
    std::size_t p = ds_offset_to_file(0x33C2);  // level1 table
    // screen 0: one PEAK (type 2, size 6 → 2 words)
    poke16(exe, p, 0x0002); poke16(exe, p + 2, 160);
    poke16(exe, p + 4, static_cast<std::uint16_t>(-12));
    p += 6;
    // separator → screen 1
    poke16(exe, p, 0x0000); p += 2;
    // screen 1: one monster (type 0x0A, size 26 → 12 words)
    poke16(exe, p, 0x000A);
    for (int i = 0; i < 12; ++i)
        poke16(exe, p + 2 + static_cast<std::size_t>(i) * 2,
               static_cast<std::uint16_t>(i * 3));
    p += 26;
    // sentinel
    poke16(exe, p, 0x00FF);

    const auto screens = read_object_table(exe, 0x33C2);
    REQUIRE(screens.size() == 2);
    REQUIRE(screens[0].size() == 1);
    CHECK(screens[0][0].type == 0x0002);
    CHECK(screens[0][0].words == std::vector<std::int16_t>{160, -12});
    REQUIRE(screens[1].size() == 1);
    CHECK(screens[1][0].type == 0x000A);
    CHECK(screens[1][0].words.size() == 12);
    CHECK(screens[1][0].words[11] == 33);
}

TEST_CASE("unknown object type throws") {
    auto exe = blank_exe();
    poke16(exe, ds_offset_to_file(0x2950), 0x0099);  // bogus type
    CHECK_THROWS_AS(read_object_table(exe, 0x2950), ExeTableError);
}

TEST_CASE("record strides match the dispatcher groupings") {
    CHECK(object_record_size(0x00) == 2);    // separator
    CHECK(object_record_size(0x0A) == 26);   // monster class
    CHECK(object_record_size(0x08) == 14);   // balloons
    CHECK(object_record_size(0x25) == 6);    // peak variant
    CHECK_THROWS_AS(object_record_size(0x28), ExeTableError);
}

TEST_CASE("all six object tables are registered") {
    CHECK(object_tables().size() == 6);
    CHECK(std::string(object_tables()[0].name) == "level1");
    CHECK(object_tables()[5].ds_offset == 0x29E2);
}

TEST_CASE("cave size table: three u16 runs compose the 53-slot array") {
    std::vector<std::uint8_t> exe(ds_offset_to_file(0x8200), 0);
    // Poke distinct synthetic widths at the start/end of each run.
    poke16(exe, ds_offset_to_file(0x7CE8), 100);           // L1 first (idx 0)
    poke16(exe, ds_offset_to_file(0x7CE8) + 21 * 2, 101);  // L1 last (idx 21)
    poke16(exe, ds_offset_to_file(0x7D14), 102);           // L5 first (idx 26)
    poke16(exe, ds_offset_to_file(0x7D14) + 12 * 2, 103);  // L5 last (idx 38)
    poke16(exe, ds_offset_to_file(0x7D2E), 104);           // L7 first (idx 39)
    poke16(exe, ds_offset_to_file(0x7D2E) + 13 * 2, 105);  // L7 last (idx 52)
    const auto tbl = read_cave_size_table(exe);
    CHECK(tbl.size() == 53);
    CHECK(tbl[0] == 100);
    CHECK(tbl[21] == 101);
    CHECK(tbl[26] == 102);
    CHECK(tbl[38] == 103);
    CHECK(tbl[39] == 104);
    CHECK(tbl[52] == 105);
    // The L3 slots (22-25) are inert filler, not read from the image.
    CHECK(tbl[22] == 224);
    CHECK(tbl[25] == 224);
}

TEST_CASE("secret score table: 10 u16 by sprite number") {
    std::vector<std::uint8_t> exe(ds_offset_to_file(0x8200), 0);
    for (int i = 0; i < 10; ++i)
        poke16(exe, ds_offset_to_file(0x8094) + static_cast<std::size_t>(i) * 2,
               static_cast<std::uint16_t>(200 + i));
    const auto tbl = read_secret_score_table(exe);
    CHECK(tbl[0] == 200);
    CHECK(tbl[9] == 209);
}

TEST_CASE("adlib sfx voices: 28-u16 records decode mod/car/waveforms") {
    std::vector<std::uint8_t> exe(ds_offset_to_file(0x8200), 0);
    const std::size_t hit = ds_offset_to_file(0x8148);
    for (int i = 0; i < 13; ++i) {
        poke16(exe, hit + static_cast<std::size_t>(i) * 2,
               static_cast<std::uint16_t>(i + 1));            // mod = 1..13
        poke16(exe, hit + 26 + static_cast<std::size_t>(i) * 2,
               static_cast<std::uint16_t>(i + 21));           // car = 21..33
    }
    poke16(exe, hit + 52, 2);   // modulator waveform
    poke16(exe, hit + 54, 1);   // carrier waveform
    const auto voices = read_adlib_sfx_voices(exe);
    CHECK(voices.hit.mod[0] == 1);
    CHECK(voices.hit.mod[12] == 13);
    CHECK(voices.hit.car[0] == 21);
    CHECK(voices.hit.car[12] == 33);
    CHECK(voices.hit.mod_wf == 2);
    CHECK(voices.hit.car_wf == 1);
    // The other two records live at their own addresses (untouched → zero).
    CHECK(voices.generic.mod[0] == 0);
    CHECK(voices.jump_apex.car_wf == 0);
}
