// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Shared surface-screen tile-list construction (OL-B2).
//
// bind_screen (the live-screen bind path) and build_surface_screen_assets
// (the read-only widescreen peek/strip compose path) used to carry two
// hand-mirrored copies of the same ~190-line tile construction — L3
// pine/trunk backdrops, the L7 lavarock/ceiling-band/stalactite branches,
// the authored placement loop — and the copies had already started to
// drift (2026-07-03 critical review, Tier-3 #2).  build_screen_tiles is
// the single constructor both call.
//
// PURITY CONTRACT (this is what makes it safe for the peek path):
//   * strictly read-only on every input — no game state, no collision
//     stamping, no RNG draws (core::global_rng is never touched);
//   * deterministic: the output depends only on the arguments;
//   * side-effect-free apart from writing `out`.
// Collision stamping and any other g/state mutation stay at the bind_screen
// call site, NOT here.

#pragma once

#include <vector>

#include "formats/mat.hpp"
#include "prepare/exe_tables.hpp"
#include "presentation/game_render.hpp"

namespace olduvai::presentation {

// L3 tile-sprite alias chain (0-based; -1 = skip draw + collision).
int resolve_sprite_idx(int level, int idx);

// L1 screen tile alias (11 -> 10); identity for every other (level, screen).
int resolve_tile_screen(int level, int screen);

// Enhanced icy-glider sea normalisation.  Flatten the water BODY (sprite 7) to
// `baseline`, and shift the water-TIED decorations by the same per-screen delta
// so they keep their position relative to the (moved) surface:
//   * sprites 16/17/18 — sub-surface detail drawn OVER the body, authored just
//     below the original sloped surface; without the shift they rise into the
//     flattened foam and hide it (the seam/notch below the player).
//   * sprite 3 — the "LEVEL n" signpost standing IN the water (screen 11);
//     without the shift its icy base hangs with a black gap above the lowered
//     water.
// RENDER-ONLY: collision was already stamped from the original tp.y in
// bind_screen BEFORE this runs, so moving these render tiles never affects
// gameplay (and sprite 7 has no collision anyway).  No-op if the screen has no
// water body.  Applied identically wherever the surface tiles are built
// (bind_screen centre + the peek/pan-strip compose) so the centre and the
// margins/pan-strip stay in sync.
void normalize_glider_water(std::vector<LevelRenderAssets::TileDraw>& tiles,
                            int baseline);

// L7 ceiling→wall junction bridge (enhanced top band): the authored ceiling
// ends in a thin end-cap (ELEML7[8]) whose art is OPAQUE BLACK around its
// spike — a backdrop-class bridge underneath is simply painted over, so the
// junction read as the spikes "cut out near the wall".  Append a bridging
// stalactite run ON TOP of the cap/thin-rock, then re-assert the wall blocks
// above its tail, so the spike row visibly continues INTO the wall.
// Self-gating: fires only on screens with a >=3-tile stalactite row AND a
// wall piece to the right of the run (S18); call sites gate on level 7 +
// extend_top_backdrop.
void l7_bridge_ceiling_to_wall(
    const prepare::LevelTiles& level_tiles, int screen,
    std::vector<LevelRenderAssets::TileDraw>& tiles);

// Everything build_screen_tiles reads, lifted out of the app's Loaded
// aggregate so the constructor stays testable without a live session.
struct ScreenTileContext {
    int level = 0;                    // internal level id (1/3/5/7)
    bool extend_top_backdrop = false; // enhanced top-HUD-band extension
    bool visual_background = false;   // PC1 levels skip column extension
    int glider_water_y = -1;          // enhanced icy sea baseline; -1 = off
    // GROT3 body index base: tile_sprites->size() before the GROT3 append
    // (== the surface tile count; L3 trunk indices are surface_count/+1).
    int surface_tile_count = 0;
    const prepare::LevelTiles* level_tiles = nullptr;  // authored placements
    // Final tile atlas the placements index into (surface tiles, plus GROT3
    // appended for L3) — read only for its size + column-extension patterns.
    const std::vector<formats::Sprite>* tile_sprites = nullptr;
};

// Compose the full render tile list for a SURFACE screen into `out`
// (cleared first): per-level backdrop rows (L3 pines/trunk, L7 lavarock +
// ceiling band + synthetic stalactites), the authored placement loop via
// resolve_sprite_idx, the L7 ceiling→wall bridge, the glider-water
// normalisation and the HUD-band column extension.  Returns the number of
// leading BACKDROP tiles (the backdrop/level split the widescreen seam pass
// layers a neighbour's overhang under).
int build_screen_tiles(const ScreenTileContext& ctx, int screen,
                       std::vector<LevelRenderAssets::TileDraw>& out);

}  // namespace olduvai::presentation
