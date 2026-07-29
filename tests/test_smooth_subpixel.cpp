// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Smooth-motion SUB-PIXEL invariant — the gate BACKLOG 3.3b step 1 asks for.
//
// WHY THIS EXISTS, and why it is a unit test rather than a replay.
//
// The whole smooth-motion lerp family was effectively ungated.  `smooth`
// requires `smooth_motion && frames <= 0 && shot.empty()`, and every gate we
// had falsified at least one term: --trace forces smooth_motion off
// (options_build.cpp), every pixel golden sets a shot path, every bounded run
// passes --play-frames.  So golden_trace, boss_golden_trace and all eight
// pixel goldens are blind to it.  Only cave_lerp reaches the path at all, and
// it covers the platform PLAYER's snap behaviour — no boss path, nothing else.
//
// That hole shipped a defect: the L6 victory drop's lerp was structurally
// correct (snapshot, snap guard, restore, timers outside the sub-loop) and
// still looked un-lerped, because it moved the INTEGER player.y while the
// renderer blitted at that integer.  +4 per logic tick is 16 OUTPUT pixels at
// hd_scale 4, so three sub-frames could only land on three of them.
//
// SO THE ASSERTION IS ABOUT GRANULARITY, NOT DISTINCTNESS.  "The sub-frames
// draw distinct positions" would have PASSED on the broken build — the integer
// lerp did produce three distinct values.  What was missing is that the
// renderer never consulted the float render base.  These tests pin exactly
// that: a sub-native change in the float position must change the output, and
// must do so ONLY when use_float_pos says to.
//
// Always-green by construction: synthetic sprites, no game files, no SDL
// window.  That matters more than usual here — this family's defects have
// twice reached a release precisely because its gates were asset-gated.
#include "doctest/doctest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "enhance/hd_asset_cache.hpp"
#include "presentation/render/boss_render.hpp"
#include "presentation/render/game_render.hpp"

using olduvai::enhance::HdAssetCache;
using olduvai::formats::Rgb;
using olduvai::formats::Sprite;
using olduvai::formats::SpriteFormat;
using olduvai::presentation::BossAssets;
using olduvai::presentation::BossPlayerState;
using olduvai::presentation::L6BossState;
using olduvai::presentation::RenderTarget;

namespace {

// 8x2 monochrome, same shape test_game_render_hd uses: one byte per row,
// set bit -> opaque colour 15.  Simplest decode path there is.
Sprite mono_sprite() {
    Sprite s;
    s.width = 8;
    s.height = 2;
    s.format = SpriteFormat::Mono1bpp;
    s.raw_pixels = {0b11111111, 0b11111111};
    return s;
}

std::vector<Rgb> pal16() {
    std::vector<Rgb> p(16, Rgb{0, 0, 0});
    p[15] = Rgb{200, 100, 50};
    return p;
}

// Enough atlas for render_l6_victory_sprites: it reaches spr[3] (the drop
// sprite), spr[28], spr[49] and spr[51 + cycle_idx], plus h3[0].
BossAssets synthetic_boss_assets() {
    BossAssets a;
    a.palette = pal16();
    a.spr.assign(64, mono_sprite());
    a.h3.assign(1, mono_sprite());
    a.bg.assign(static_cast<std::size_t>(320) * 200 * 4, 0);
    return a;
}

constexpr int kScale = 2;
constexpr int kW = 320 * kScale;
constexpr int kH = 200 * kScale;

struct Canvas {
    std::vector<std::uint8_t> px;
    HdAssetCache cache;
    std::string profile = "omniscale";
    Canvas() : px(static_cast<std::size_t>(kW) * kH * 4, 0) {}
    RenderTarget target() {
        return RenderTarget{px.data(), kW, kH, kScale, &cache, &profile};
    }
};

// The L6 victory drop, rendered at a given float y.
std::vector<std::uint8_t> draw_victory_at(const BossAssets& a, bool use_float,
                                          float fy) {
    Canvas c;
    RenderTarget t = c.target();
    t.use_float_pos = use_float;
    t.player_fx = 100.0f;
    t.player_fy = fy;
    BossPlayerState p{};
    p.x = 100;
    p.y = 100;            // < kFloorY (160) -> the DROP branch
    p.facing_left = false;
    L6BossState boss{};
    boss.cycle_idx = 0;
    olduvai::presentation::render_l6_victory_sprites(t, a, p, boss);
    return c.px;
}

}  // namespace

TEST_CASE("L6 victory drop honours the float render base (the 2026-07-28 bug)") {
    const auto a = synthetic_boss_assets();

    // A quarter of a native pixel — invisible to an integer blit, and exactly
    // the granularity smooth motion exists to provide at hd_scale > 1.
    const auto at_100_00 = draw_victory_at(a, /*use_float=*/true, 100.00f);
    const auto at_100_50 = draw_victory_at(a, /*use_float=*/true, 100.50f);

    CHECK_MESSAGE(at_100_00 != at_100_50,
                  "a sub-native change in player_fy must move the drop sprite: "
                  "this is the regression that made a correct lerp invisible");
}

TEST_CASE("use_float_pos is what switches the granularity on") {
    const auto a = synthetic_boss_assets();

    // With the flag OFF the float fields must be ignored entirely — the
    // renderer falls back to the integer logic position, so both draws are
    // the same frame.  This is the half that keeps classic byte-identical.
    const auto off_a = draw_victory_at(a, /*use_float=*/false, 100.00f);
    const auto off_b = draw_victory_at(a, /*use_float=*/false, 100.75f);
    CHECK_MESSAGE(off_a == off_b,
                  "use_float_pos=false must ignore player_fy — classic mode "
                  "renders from the integer position only");

    // And the integer fallback must agree with the float path fed the same
    // integer: the int blit_sprite overload forwards to the float core, so
    // these are the identical call.
    const auto on_exact = draw_victory_at(a, /*use_float=*/true, 100.0f);
    CHECK_MESSAGE(off_a == on_exact,
                  "float path fed an exact integer must equal the integer path");
}

TEST_CASE("the fight player already had this — pin it so a sweep cannot lose it") {
    // render_boss_player_fb is where the float base has worked all along, and
    // it is the reference the victory drop was measured against.  BACKLOG
    // 3.3b's unification touches both; this is the before-picture that must
    // survive it.
    const auto a = synthetic_boss_assets();

    auto draw_player_at = [&](bool use_float, float fy) {
        Canvas c;
        RenderTarget t = c.target();
        t.use_float_pos = use_float;
        t.player_fx = 100.0f;
        t.player_fy = fy;
        BossPlayerState p{};
        p.x = 100;
        p.y = 100;
        p.sprite = 0;
        p.club_spr = -1;
        olduvai::presentation::render_boss_player_fb(t, p, a.spr, a.palette);
        return c.px;
    };

    CHECK_MESSAGE(draw_player_at(true, 100.00f) != draw_player_at(true, 100.50f),
                  "fight player must move on a sub-native player_fy change");
    CHECK_MESSAGE(draw_player_at(false, 100.00f) == draw_player_at(false, 100.75f),
                  "fight player must ignore player_fy when use_float_pos is off");
}
