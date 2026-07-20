// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen static-background COMPOSITION tests — review 2026-07-03 T4.4.
// tile_patterns covers the pure seam/straddler/bridge helpers; these cover
// the composition itself (compose_static_wide_bg_native) and the HD cache's
// keying invariants (get_static_wide_bg_hd), whose failure class is a
// SILENT wrong frame — a cached seam-bearing buffer served to an empty-seam
// caller, or a stale margin after the peek was rebuilt.  Pure CPU: no SDL.

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "presentation/game_render.hpp"
#include "systems/player.hpp"

// GCC's -Wdangling-reference misfires on get_static_wide_bg_hd: it returns a
// reference to the compose CACHE, but the {} temporaries among its arguments
// make the heuristic suspect the return binds to one. Clang has no such
// warning (an unknown -W name in the pragma would itself warn there).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wdangling-reference"
#endif

using olduvai::presentation::FrameBuffer;
using olduvai::presentation::LevelRenderAssets;
using olduvai::formats::Rgb;
using olduvai::formats::Sprite;
using olduvai::formats::SpriteFormat;
using TileDraw = LevelRenderAssets::TileDraw;

namespace {

// Solid 8x8 monochrome tile (all bits set -> colour 15).
Sprite solid8() {
    Sprite s;
    s.width = 8;
    s.height = 8;
    s.format = SpriteFormat::Mono1bpp;
    s.raw_pixels.assign(8, 0xFF);
    return s;
}

LevelRenderAssets make_assets() {
    LevelRenderAssets a;
    a.visual_background = false;
    a.bg_fill_index = 1;                       // dark solid fill
    a.palette.assign(16, Rgb{0, 0, 0});
    a.palette[1] = Rgb{10, 20, 30};            // bg fill
    a.palette[15] = Rgb{200, 100, 50};         // tile colour
    a.tile_sprites = {solid8()};
    return a;
}

olduvai::systems::SystemsState make_state() {
    olduvai::systems::SystemsState st;
    st.current_level = 5;      // tile level, no L1/L3/L7 special branch
    st.current_screen = 3;
    return st;
}

std::uint64_t fnv(const std::vector<std::uint8_t>& v) {
    std::uint64_t h = 1469598103934665603ull;
    for (const std::uint8_t b : v) { h ^= b; h *= 1099511628211ull; }
    return h;
}

// Solid-colour native neighbour frame.
FrameBuffer solid_frame(std::uint8_t r) {
    FrameBuffer f;
    for (std::size_t i = 0; i < f.px.size(); i += 4) {
        f.px[i] = r; f.px[i + 1] = 0; f.px[i + 2] = 0; f.px[i + 3] = 255;
    }
    return f;
}

}  // namespace

TEST_CASE("wide compose: margins carry the neighbour pixels, centre the bg") {
    auto assets = make_assets();
    auto st = make_state();
    const int margin = 24;
    FrameBuffer left = solid_frame(111), right = solid_frame(222);
    std::vector<std::uint8_t> out;
    olduvai::presentation::compose_static_wide_bg_native(
        st, assets, margin, &left, &right, nullptr, out);
    const int w = 320 + 2 * margin;
    REQUIRE(out.size() == static_cast<std::size_t>(w) * 200 * 4);
    // Left margin pixel row 100 col 5 = left neighbour's column 320-margin+5.
    const std::size_t lo = (100 * static_cast<std::size_t>(w) + 5) * 4;
    CHECK(out[lo] == 111);
    // Right margin col w-5 = right neighbour's early columns.
    const std::size_t ro = (100 * static_cast<std::size_t>(w) + (w - 5)) * 4;
    CHECK(out[ro] == 222);
    // Centre col margin+10 = the level's own bg fill.
    const std::size_t co = (100 * static_cast<std::size_t>(w) + margin + 10) * 4;
    CHECK(out[co] == 10);
    CHECK(out[co + 1] == 20);
    CHECK(out[co + 2] == 30);
}

TEST_CASE("wide compose: straddlers draw CENTRE-ONLY, bridges reach margins") {
    // A right-seam bridge tile at x=316 must paint margin pixels (x 320..323
    // in screen coords); the SAME placement given as a straddler must NOT
    // touch the margin (the peek already holds its margin part with the
    // neighbour's authored z-order — re-blitting buried S14's dirt row).
    auto assets = make_assets();
    auto st = make_state();
    const int margin = 24;
    FrameBuffer right = solid_frame(222);
    // Right-side lists are in coords where the compose's +320 shift lands
    // them on-screen (the caller stores b.x - 320): -4 -> screen-x 316,
    // spanning centre 316..319 and margin 320..323.
    const std::vector<TileDraw> place = {{0, -4, 96}};
    std::vector<std::uint8_t> as_bridge, as_straddler, none;
    olduvai::presentation::compose_static_wide_bg_native(
        st, assets, margin, nullptr, &right, nullptr, as_bridge,
        {}, {}, {}, /*right_bridge=*/place);
    olduvai::presentation::compose_static_wide_bg_native(
        st, assets, margin, nullptr, &right, nullptr, as_straddler,
        {}, /*right_seam=*/place, {}, {});
    olduvai::presentation::compose_static_wide_bg_native(
        st, assets, margin, nullptr, &right, nullptr, none);
    const int w = 320 + 2 * margin;
    // Margin pixel at screen-x 321 (buffer col margin+321), row 100.
    const std::size_t mo = (100 * static_cast<std::size_t>(w) +
                            margin + 321) * 4;
    CHECK(as_bridge[mo] == 200);        // bridge painted the tile colour
    CHECK(as_straddler[mo] == 222);     // straddler left the peek pixel
    // Centre pixel at screen-x 317: both variants paint it.
    const std::size_t ce = (100 * static_cast<std::size_t>(w) +
                            margin + 317) * 4;
    CHECK(as_bridge[ce] == 200);
    CHECK(as_straddler[ce] == 200);
    CHECK(none[ce] == 10);              // and without any list: bg fill
}

TEST_CASE("HD wide cache: empty-seam caller never gets a seam-bearing frame") {
    auto assets = make_assets();
    auto st = make_state();
    const int margin = 24;
    const std::string profile = "retro";
    FrameBuffer right = solid_frame(222);
    const std::vector<TileDraw> bridge = {{0, -4, 96}};   // -> screen-x 316
    const auto& with = olduvai::presentation::get_static_wide_bg_hd(
        st, assets, /*scale=*/2, profile, margin, nullptr, -1, &right, 4,
        nullptr, {}, {}, {}, bridge, /*peek_generation=*/1);
    const std::uint64_t h_with = fnv(with);
    const auto& without = olduvai::presentation::get_static_wide_bg_hd(
        st, assets, 2, profile, margin, nullptr, -1, &right, 4,
        nullptr, {}, {}, {}, {}, 1);
    const std::uint64_t h_without = fnv(without);
    CHECK(h_with != h_without);   // cache collision = silent wrong frame
    // Same args again -> cache hit must reproduce the same pixels.
    const auto& again = olduvai::presentation::get_static_wide_bg_hd(
        st, assets, 2, profile, margin, nullptr, -1, &right, 4,
        nullptr, {}, {}, {}, bridge, 1);
    CHECK(fnv(again) == h_with);
}

TEST_CASE("HD wide cache: peek_generation invalidates baked margins") {
    // The key can't see the peek PIXELS (a destroyed rock near the seam is
    // baked into them) — bumping the generation must re-compose.
    auto assets = make_assets();
    auto st = make_state();
    const int margin = 24;
    const std::string profile = "retro";
    FrameBuffer right = solid_frame(50);
    const auto h1 = fnv(olduvai::presentation::get_static_wide_bg_hd(
        st, assets, 2, profile, margin, nullptr, -1, &right, 4,
        nullptr, {}, {}, {}, {}, /*peek_generation=*/7));
    // Mutate the peek content (same screen index!) — a stale cache would
    // still serve h1 without the generation bump.
    right = solid_frame(99);
    const auto h_stale = fnv(olduvai::presentation::get_static_wide_bg_hd(
        st, assets, 2, profile, margin, nullptr, -1, &right, 4,
        nullptr, {}, {}, {}, {}, 7));
    const auto h_fresh = fnv(olduvai::presentation::get_static_wide_bg_hd(
        st, assets, 2, profile, margin, nullptr, -1, &right, 4,
        nullptr, {}, {}, {}, {}, 8));
    CHECK(h_stale == h1);    // documented limitation: same generation = cached
    CHECK(h_fresh != h1);    // the bump is the invalidation mechanism
}
