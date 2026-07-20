// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Unit tests for the vertical tile-column pattern primitive (tile_patterns) —
// the consolidation of the three ad-hoc trunk detectors (spike
// the pattern-matching spike notes (internal)).  Pure placement-list logic: no SDL.

#include <doctest/doctest.h>

#include <algorithm>

#include "presentation/tile_patterns.hpp"

using olduvai::presentation::LevelRenderAssets;
using olduvai::presentation::tile_patterns::extend_columns_to_top;
using olduvai::presentation::tile_patterns::seam_straddling_tiles;
using TileDraw = LevelRenderAssets::TileDraw;

namespace {

// Minimal sprite sheet: index → (w, h).
std::vector<olduvai::formats::Sprite> sheet(
    const std::vector<std::pair<int, int>>& dims) {
    std::vector<olduvai::formats::Sprite> out;
    for (const auto& [w, h] : dims) {
        olduvai::formats::Sprite s;
        s.width = w;
        s.height = h;
        out.push_back(std::move(s));
    }
    return out;
}

int count_at(const std::vector<TileDraw>& tiles, int spr, int x, int y) {
    return static_cast<int>(std::count_if(
        tiles.begin(), tiles.end(), [&](const TileDraw& t) {
            return t.sprite_idx == spr && t.x == x && t.y == y;
        }));
}

}  // namespace

TEST_CASE("tile_patterns: uniform trunk column extends to row 0") {
    // Dark Woods forest trunk: spr 0 (48x57) stacked at y=10/67/124.
    auto spr = sheet({{48, 57}});
    std::vector<TileDraw> tiles = {
        {0, 256, 10}, {0, 256, 67}, {0, 256, 124}};
    extend_columns_to_top(tiles, spr);
    // One tile added at y = 10-57 = -47 (further would be fully off-screen).
    CHECK(tiles.size() == 4);
    CHECK(count_at(tiles, 0, 256, -47) == 1);
}

TEST_CASE("tile_patterns: mixed-sprite giant trunk continues its ALTERNATION") {
    // Giant level-end trunk: authored as an alternating pair — spr 0 (h=42)
    // over spr 1 (h=41) over spr 0 …  The upward continuation must reuse the
    // column's own period (next above spr0 is spr1), NOT repeat the top tile:
    // spr0-over-spr0 visibly breaks the bark's authored adjacency.  Also
    // exercises the mixed-height above-probe (42/41).
    auto spr = sheet({{144, 42}, {144, 41}});
    std::vector<TileDraw> tiles = {{0, 176, 9}, {1, 176, 51}};
    extend_columns_to_top(tiles, spr);
    // Above spr0@9 comes spr1 (h=41) at 9-41 = -32; the next (spr0 at -74)
    // would be fully off-screen.
    CHECK(count_at(tiles, 1, 176, -32) == 1);
    CHECK(count_at(tiles, 0, 176, -33) == 0);   // no top-tile repeat
    CHECK(tiles.size() == 3);
}

TEST_CASE("tile_patterns: lone tiles and non-top tiles do not extend") {
    auto spr = sheet({{48, 57}, {64, 49}});
    std::vector<TileDraw> tiles = {
        {1, 100, 12},                 // lone bush in the band — no chain
        {0, 256, 10}, {0, 256, 67},   // column — only its TOP may extend
    };
    extend_columns_to_top(tiles, spr);
    CHECK(count_at(tiles, 1, 100, 12 - 49) == 0);   // bush untouched
    CHECK(count_at(tiles, 0, 256, -47) == 1);       // column top extended once
    CHECK(count_at(tiles, 0, 256, 67 - 57) == 1);   // = the ORIGINAL y=10 tile
    CHECK(tiles.size() == 4);
}

TEST_CASE("tile_patterns: seam columns collected per straddled edge") {
    auto spr = sheet({{48, 57}});
    std::vector<TileDraw> tiles = {
        {0, 288, 9},  {0, 288, 66},    // right-straddling column (x2=336)
        {0, -32, 9},  {0, -32, 66},    // left-straddling column (x2=16)
        {0, 128, 9},  {0, 128, 66},    // interior column — never collected
    };
    auto right = seam_straddling_tiles(tiles, spr, /*right_edge=*/true);
    auto left = seam_straddling_tiles(tiles, spr, /*right_edge=*/false);
    CHECK(right.size() == 2);
    CHECK(left.size() == 2);
    for (const auto& t : right) CHECK(t.x == 288);
    for (const auto& t : left) CHECK(t.x == -32);
}

TEST_CASE("tile_patterns: width-mismatched adjacency is not a pattern") {
    // Dark Woods S15: a door piece (48x35) at y=14 coincidentally ends exactly
    // where a horizontal beam row (16x9) starts.  That adjacency is NOT a
    // repeating column — continuing it duplicated the door + floated a beam in
    // the HUD band.  The width gate must reject it.
    auto spr = sheet({{48, 35}, {16, 9}});
    std::vector<TileDraw> tiles = {{0, 224, 14}, {1, 224, 49}};
    extend_columns_to_top(tiles, spr);
    CHECK(tiles.size() == 2);   // nothing added
}

TEST_CASE("tile_patterns: seam row bridge fills an authored decor hole") {
    // L7 S1|S2 jumppad rail: spr 0 (16x12) at y=97 — S1's contiguous run ends
    // at 304 (tile at 288), S2 resumes at its x=0 (=320).  The 16px hole is
    // bridged with one tile at (304, 97), in A (left-screen) coords.
    auto spr = sheet({{16, 12}});
    std::vector<TileDraw> a = {{0, 272, 97}, {0, 288, 97}};
    std::vector<TileDraw> b = {{0, 0, 97}, {0, 16, 97}};
    auto out = olduvai::presentation::tile_patterns::seam_row_bridges(a, 0, b, 0, spr);
    REQUIRE(out.size() == 1);
    CHECK(out[0].sprite_idx == 0);
    CHECK(out[0].x == 304);
    CHECK(out[0].y == 97);
}

TEST_CASE("tile_patterns: seam bridge refuses walkable ground band") {
    // A ground row (y + h > 150) broken at the seam is an authored JUMP PIT
    // (L5 icy 0|1 spr7 y=164) — painting rock over it would fake a floor.
    auto spr = sheet({{80, 48}});
    std::vector<TileDraw> a = {{0, 144, 164}, {0, 224, 164}};   // ends 304
    std::vector<TileDraw> b = {{0, 16, 164}, {0, 96, 164}};     // resumes 336
    CHECK(olduvai::presentation::tile_patterns::seam_row_bridges(a, 0, b, 0, spr)
              .empty());
}

TEST_CASE("tile_patterns: seam bridge refuses spaced features") {
    // Fence posts (16x57) at a 32px stride: the seam gap equals the natural
    // spacing — neither side has a CONTIGUOUS (stride == w) run, so no
    // bridge (L7 7|8 spr13 posts).
    auto spr = sheet({{16, 57}});
    std::vector<TileDraw> a = {{0, 256, 103}, {0, 288, 103}};   // 32 stride
    std::vector<TileDraw> b = {{0, 16, 103}, {0, 48, 103}};
    CHECK(olduvai::presentation::tile_patterns::seam_row_bridges(a, 0, b, 0, spr)
              .empty());
}

TEST_CASE("tile_patterns: lone straddling tile IS collected") {
    // Dark Woods S16 authors a lone foliage tile (spr 5, 80x53) at x=256 —
    // x2=336 crosses the right edge.  An earlier column-only gate skipped it
    // and the bush stayed cut off at S17's left seam; ANY straddler must be
    // completed on the other side.
    auto spr = sheet({{80, 53}});
    std::vector<TileDraw> tiles = {{0, 256, 53}};   // lone bush, no chain
    auto right = seam_straddling_tiles(tiles, spr, /*right_edge=*/true);
    REQUIRE(right.size() == 1);
    CHECK(right[0].x == 256);
    CHECK(seam_straddling_tiles(tiles, spr, /*right_edge=*/false).empty());
}
