// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Boss-arena rendering: the BossAssets bundle + the per-level sprite/frame and
// victory renderers, moved verbatim out of boss_app.cpp so that file is the
// fight orchestrator, not also the renderer (SOC roadmap: boss_render).
// Asset LOADING (load_boss_assets / erase_pip_column) stays in boss_app.cpp
// with its file reader; these are pure rendering into a RenderTarget.

#pragma once

#include <cstdint>
#include <vector>

#include "formats/mat.hpp"      // formats::Sprite
#include "formats/pc1.hpp"      // formats::Rgb
#include "presentation/game_render.hpp"  // RenderTarget
#include "systems/boss.hpp"     // BossPlayerState
#include "systems/boss_l2.hpp"  // L2BossState
#include "systems/boss_l4.hpp"  // L4BossState
#include "systems/boss_l6.hpp"  // L6BossState

namespace olduvai::presentation {

using formats::Rgb;
using formats::Sprite;
using systems::BossPlayerState;
using systems::L2BossState;
using systems::L4BossState;
using systems::L6BossState;

struct BossAssets {
    std::vector<std::uint8_t> bg;   // RGBA 320x200 (pip drain mutates it)
    std::vector<Sprite> spr;        // boss sprite atlas
    std::vector<Sprite> elem;       // arena body pieces (L2)
    std::vector<Sprite> h1, h2, h3, h4; // L6 body poses A/B/C + head sheet (H4)
    std::vector<Sprite> charset;
    std::vector<Rgb> palette;
    // Pause-menu bone cursor: L1SPR[33] score-bone + FOND1 palette (same
    // pointer art the surface pause / main menu use).
    std::vector<Sprite> bone_atlas;
    std::vector<Rgb> bone_palette;
};

// Pure boss-arena renderers (declared here; defined in boss_render.cpp).
void render_boss_player_fb(RenderTarget& t, const BossPlayerState& p,
                           const std::vector<Sprite>& atlas,
                           const std::vector<Rgb>& pal);
void blit_bg(RenderTarget& t, const BossAssets& a);
void blit_at(RenderTarget& t, const std::vector<Sprite>& atlas, int idx,
             const std::vector<Rgb>& pal, float x, float y);
void render_l2_sprites(RenderTarget& t, const BossAssets& a,
                       const BossPlayerState& p, const L2BossState& boss);
void render_l2_frame(RenderTarget& t, const BossAssets& a,
                     const BossPlayerState& p, const L2BossState& boss);
void render_l4_sprites(RenderTarget& t, const BossAssets& a,
                       const BossPlayerState& p, const L4BossState& boss);
void render_l4_frame(RenderTarget& t, const BossAssets& a,
                     const BossPlayerState& p, const L4BossState& boss);
void render_l6_sprites(RenderTarget& t, const BossAssets& a,
                       const BossPlayerState& p, const L6BossState& boss);
void render_l6_frame(RenderTarget& t, const BossAssets& a,
                     const BossPlayerState& p, const L6BossState& boss);
void render_l2_victory_sprites(RenderTarget& t, const BossAssets& a,
                               const BossPlayerState& p, int flash_frame);
void render_l2_victory_frame(RenderTarget& t, const BossAssets& a,
                              const BossPlayerState& p, int flash_frame);
void render_l4_victory_sprites(RenderTarget& t, const BossAssets& a,
                               const BossPlayerState& p,
                               const L4BossState& boss);
void render_l4_victory_frame(RenderTarget& t, const BossAssets& a,
                              const BossPlayerState& p, const L4BossState& boss);
void render_l6_victory_frame(RenderTarget& t,
                              const std::vector<std::uint8_t>& victory_bg_px,
                              const BossAssets& a,
                              const BossPlayerState& p,
                              const L6BossState& boss);
void render_l6_victory_sprites(RenderTarget& t, const BossAssets& a,
                               const BossPlayerState& p, const L6BossState& boss);

}  // namespace olduvai::presentation
