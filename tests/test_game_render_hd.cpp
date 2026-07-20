// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include <doctest/doctest.h>

#include "presentation/game_render.hpp"
#include "systems/player.hpp"

#include <cstdint>
#include <string>
#include <vector>

using olduvai::presentation::FrameBuffer;
using olduvai::presentation::RenderTarget;
using olduvai::enhance::HdAssetCache;
using olduvai::formats::Rgb;
using olduvai::formats::Sprite;
using olduvai::formats::SpriteFormat;

namespace {
// Minimal synthetic sprite: 8×2 monochrome (set bit → opaque colour 15).
// Mono is the simplest decode path (1 byte/row, no 5-plane crafting).
Sprite mono_sprite() {
    Sprite s;
    s.width = 8;
    s.height = 2;
    s.format = SpriteFormat::Mono1bpp;
    // row 0: x0 + x2 set; row 1: x5 + x7 set.
    s.raw_pixels = {0b10100000, 0b00000101};
    return s;
}
std::vector<Rgb> pal16() {
    std::vector<Rgb> p(16, Rgb{0, 0, 0});
    p[15] = Rgb{200, 100, 50};
    return p;
}
}  // namespace

TEST_CASE("RenderTarget scale 1 equals the legacy FrameBuffer blit") {
    const auto s = mono_sprite();
    const auto pal = pal16();
    FrameBuffer a;
    olduvai::presentation::blit_sprite(a, s, pal, 10, 20);
    FrameBuffer b;
    RenderTarget t{b.px.data(), 320, 200, 1, nullptr, nullptr};
    olduvai::presentation::blit_sprite(t, s, pal, 10, 20);
    CHECK(a.px == b.px);
}

TEST_CASE("RenderTarget scale 1 equals legacy blit when flipped") {
    const auto s = mono_sprite();
    const auto pal = pal16();
    FrameBuffer a;
    olduvai::presentation::blit_sprite(a, s, pal, 30, 40, /*flip_h=*/true);
    FrameBuffer b;
    RenderTarget t{b.px.data(), 320, 200, 1, nullptr, nullptr};
    olduvai::presentation::blit_sprite(t, s, pal, 30, 40, /*flip_h=*/true);
    CHECK(a.px == b.px);
}

TEST_CASE("blit_sprite_keyed scale 1 equals legacy keyed blit") {
    const auto s = mono_sprite();
    const auto pal = pal16();
    FrameBuffer a;
    olduvai::presentation::blit_sprite_keyed(a, s, pal, 12, 14);
    FrameBuffer b;
    RenderTarget t{b.px.data(), 320, 200, 1, nullptr, nullptr};
    olduvai::presentation::blit_sprite_keyed(t, s, pal, 12, 14);
    CHECK(a.px == b.px);
}

// ── origin_x: the kept widescreen entity-overflow ─────────────────────────────
// The overflow pass draws live entities over the WIDE buffer at origin_x =
// margin so sprites crossing the 320 edge spill into the neighbour-terrain
// margins instead of being hard-clipped.  origin_x = 0 must stay byte-identical
// (every existing non-widescreen call), origin_x = N shifts by N native columns,
// and an over-the-edge blit must clip (no OOB write) while still landing pixels
// in the margin.
TEST_CASE("origin_x = 0 leaves the blit byte-identical (overflow opt-out)") {
    const auto s = mono_sprite();
    const auto pal = pal16();
    FrameBuffer a;
    olduvai::presentation::blit_sprite(a, s, pal, 50, 60);
    FrameBuffer b;
    RenderTarget t{b.px.data(), 320, 200, 1, nullptr, nullptr};
    t.origin_x = 0;   // explicit default
    olduvai::presentation::blit_sprite(t, s, pal, 50, 60);
    CHECK(a.px == b.px);
}

TEST_CASE("origin_x shifts the blit by exactly origin_x native columns") {
    const auto s = mono_sprite();
    const auto pal = pal16();
    // Reference: blit at x = 50 + 7.
    FrameBuffer ref;
    olduvai::presentation::blit_sprite(ref, s, pal, 57, 60);
    // Under test: blit at x = 50 with origin_x = 7.
    FrameBuffer und;
    RenderTarget t{und.px.data(), 320, 200, 1, nullptr, nullptr};
    t.origin_x = 7;
    olduvai::presentation::blit_sprite(t, s, pal, 50, 60);
    CHECK(ref.px == und.px);
}

TEST_CASE("origin_x overflow past the native edge clips, no OOB write") {
    const auto s = mono_sprite();   // 8 wide
    const auto pal = pal16();
    // Wide native buffer (320 + 2*8 margin); place a sprite straddling x=320.
    const int wmargin = 8;
    const int ww = 320 + 2 * wmargin;
    std::vector<std::uint8_t> wide(static_cast<std::size_t>(ww) * 200 * 4, 0);
    RenderTarget t{wide.data(), ww, 200, 1, nullptr, nullptr};
    t.origin_x = wmargin;
    // Native x = 316 → wide x = 316 + 8 = 324..331, all inside [0,ww). No OOB.
    olduvai::presentation::blit_sprite(t, s, pal, 316, 100);
    // Sanity: at least one opaque pixel landed in the right margin, i.e. past
    // the center region (wide x >= 320 + margin).  mono_sprite row 1 sets x5/x7
    // → wide x 329/331, which are >= 328.
    bool overflowed = false;
    for (int y = 100; y < 102 && !overflowed; ++y)
        for (int x = 320 + wmargin; x < ww; ++x) {
            const std::size_t o = (static_cast<std::size_t>(y) * ww + x) * 4;
            if (wide[o + 3] == 255) { overflowed = true; break; }
        }
    CHECK(overflowed);
}

// ── Club-mechanic regression: draw-state advances EXACTLY ONCE per frame ──────
// The widescreen present path draws the entities a SECOND time over the wide
// buffer (the kept entity-overflow pass).  If BOTH draws advanced the player
// draw-state (the club_flag swing decrement, death/cave-warp clears) the 2-frame
// club swing collapsed and missed the hit window — the reported flicker/no-hit
// bug.  The fix: the SINGLE AUTHORITATIVE advance is the main fb compose
// (advance_state = true, default); the widescreen overflow pass draws with
// advance_state = false (visual only).  These tests pin the invariant:
//   * advance_state = true  → one draw_entities decrements club_flag by 1.
//   * advance_state = false → draw_entities draws but does NOT advance.
//   * one widescreen FRAME = one advancing fb draw + one non-advancing overflow
//     draw = net advance of exactly 1.
TEST_CASE("one draw_entities frame mid-swing decrements club_flag by exactly 1") {
    using olduvai::systems::SystemsState;
    olduvai::presentation::LevelRenderAssets a;
    a.palette = pal16();
    // entity_sprites must cover the player sprite (index 0) and the club
    // overlay sprite (kClubTbl[*].spr = 13) so the weapon-overlay block runs.
    a.entity_sprites.assign(16, mono_sprite());

    SystemsState st;                 // clean defaults: alive, not glider, screen 0
    st.player.sprite = 0;            // kSprPlayerStand — valid → weapon block runs
    st.player.club_flag = 2;         // mid-swing: first of the two club frames

    FrameBuffer fb;
    RenderTarget t{fb.px.data(), 320, 200, 1, nullptr, nullptr};
    olduvai::presentation::draw_entities(t, st, a, /*draw_player=*/true);

    // EXACTLY ONE advance: 2 → 1, NOT 2 → 0 (double-advance) and NOT 2 (suppressed).
    CHECK(st.player.club_flag == 1);

    // A second frame completes the swing: 1 → 0.
    olduvai::presentation::draw_entities(t, st, a, /*draw_player=*/true);
    CHECK(st.player.club_flag == 0);
}

TEST_CASE("advance_state=false overflow draw does NOT advance club_flag") {
    using olduvai::systems::SystemsState;
    olduvai::presentation::LevelRenderAssets a;
    a.palette = pal16();
    a.entity_sprites.assign(16, mono_sprite());

    SystemsState st;
    st.player.sprite = 0;
    st.player.club_flag = 2;

    // The widescreen overflow pass: draws the club sprite but must NOT mutate
    // gameplay state (advance_state = false).
    FrameBuffer fb;
    RenderTarget t{fb.px.data(), 320, 200, 1, nullptr, nullptr};
    t.advance_state = false;
    olduvai::presentation::draw_entities(t, st, a, /*draw_player=*/true);
    CHECK(st.player.club_flag == 2);   // unchanged — purely visual

    // A SECOND overflow draw also leaves it untouched (no creeping advance).
    olduvai::presentation::draw_entities(t, st, a, /*draw_player=*/true);
    CHECK(st.player.club_flag == 2);
}

TEST_CASE("one widescreen FRAME (fb advance + overflow no-advance) nets exactly 1") {
    using olduvai::systems::SystemsState;
    olduvai::presentation::LevelRenderAssets a;
    a.palette = pal16();
    a.entity_sprites.assign(16, mono_sprite());

    SystemsState st;
    st.player.sprite = 0;
    st.player.club_flag = 2;          // mid-swing

    // FRAME 1: the authoritative main fb compose (advance_state = true) ...
    FrameBuffer fb;
    RenderTarget main{fb.px.data(), 320, 200, 1, nullptr, nullptr};
    olduvai::presentation::draw_entities(main, st, a, /*draw_player=*/true);
    // ... then the widescreen overflow pass (advance_state = false).
    std::vector<std::uint8_t> wide(static_cast<std::size_t>(336) * 200 * 4, 0);
    RenderTarget over{wide.data(), 336, 200, 1, nullptr, nullptr};
    over.origin_x = 8;
    over.advance_state = false;
    olduvai::presentation::draw_entities(over, st, a, /*draw_player=*/true);
    CHECK(st.player.club_flag == 1);   // net exactly 1 across the whole frame

    // FRAME 2: completes the swing → 0 (advance) + overflow no-op → stays 0.
    olduvai::presentation::draw_entities(main, st, a, /*draw_player=*/true);
    olduvai::presentation::draw_entities(over, st, a, /*draw_player=*/true);
    CHECK(st.player.club_flag == 0);
}

TEST_CASE("dead player draw clears club_flag once (death/cave-warp path)") {
    using olduvai::systems::SystemsState;
    olduvai::presentation::LevelRenderAssets a;
    a.palette = pal16();
    a.entity_sprites.assign(16, mono_sprite());

    SystemsState st;
    st.player.sprite = 0;
    st.player.club_flag = 2;
    st.player.death_counter = 1;     // death path: club_flag := 0 once

    FrameBuffer fb;
    RenderTarget t{fb.px.data(), 320, 200, 1, nullptr, nullptr};
    olduvai::presentation::draw_entities(t, st, a, /*draw_player=*/true);
    CHECK(st.player.club_flag == 0);

    // The overflow pass on the same dead frame must NOT re-clear / mutate.
    st.player.club_flag = 0;
    RenderTarget over{fb.px.data(), 320, 200, 1, nullptr, nullptr};
    over.advance_state = false;
    olduvai::presentation::draw_entities(over, st, a, /*draw_player=*/true);
    CHECK(st.player.club_flag == 0);
}

TEST_CASE("HD RenderTarget blits an upscaled sprite at scaled coordinates") {
    const auto s = mono_sprite();
    const auto pal = pal16();
    HdAssetCache cache;
    std::string prof = "omniscale";
    const int scale = 2;
    std::vector<std::uint8_t> hd(
        static_cast<std::size_t>(320 * scale) * 200 * scale * 4, 0);
    RenderTarget t{hd.data(), 320 * scale, 200 * scale, scale, &cache, &prof};
    olduvai::presentation::blit_sprite(t, s, pal, 5, 7);
    // Source pixel (0,0) is opaque → its 2× block lands at (5*2, 7*2)=(10,14)
    // and stays opaque (alpha re-applied from the opaque source pixel).
    auto at = [&](int x, int y, int c) {
        return hd[(static_cast<std::size_t>(y) * t.w + x) * 4 + c];
    };
    CHECK(at(10, 14, 3) == 255);
    // Smooth (omniscale) scaling anti-aliases sprite edges, so a transparent
    // source pixel ADJACENT to opaque colour (the gap at source (1,0), between
    // opaque x0 and x2) is now partially composited — the HD edge-smoothing
    // fix.  The invariant that still holds: pixels well OUTSIDE the 8×2 sprite
    // stay fully transparent.
    CHECK(at(10, 60, 3) == 0);    // far below the sprite
    CHECK(at(120, 14, 3) == 0);   // far right of the sprite
}
