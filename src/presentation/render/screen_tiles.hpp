// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Surface-screen tile-list construction, shared by bind_screen and the
// read-only widescreen peek/strip compose.  Pure: read-only on every input, no
// RNG, no collision stamping, deterministic; writes only `out`.  Collision and
// state changes stay at the bind_screen call site.

#pragma once

#include <vector>

#include "formats/mat.hpp"
#include "prepare/exe_tables.hpp"
#include "presentation/render/game_render.hpp"

namespace olduvai::presentation {

// L3 tile-sprite alias chain (0-based; -1 = skip draw + collision).
int resolve_sprite_idx(int level, int idx);

// L1 screen tile alias (11 -> 10); identity for every other (level, screen).
int resolve_tile_screen(int level, int screen);

// Enhanced L5 glider sea: flatten the water body (sprite 7) to `baseline` and
// shift water-tied decorations by the same per-screen delta: sprites 16/17/18
// (sub-surface detail over the body) and sprite 3 (the "LEVEL n" signpost in
// the water, screen 11).  Render only: collision was stamped from the original
// y first, and sprite 7 has none.  No-op without a water body.  Applied
// wherever surface tiles are built, so centre and margins agree.
void normalize_glider_water(std::vector<LevelRenderAssets::TileDraw>& tiles,
                            int baseline);

// L7 ceiling -> wall junction (enhanced top band): the ceiling's end cap
// (ELEML7[8]) is opaque black around its spike and hides any bridge under it,
// so append a stalactite run on top of the cap and re-assert the wall blocks
// above its tail.  Self-gating: needs a >= 3-tile stalactite row with a wall
// piece to its right (S18); callers gate on level 7 + extend_top_backdrop.
void l7_bridge_ceiling_to_wall(
    const prepare::LevelTiles& level_tiles, int screen,
    std::vector<LevelRenderAssets::TileDraw>& tiles);

// Everything build_screen_tiles reads, so it is testable without a session.
struct ScreenTileContext {
    int level = 0;                    // internal level id (1/3/5/7)
    bool extend_top_backdrop = false; // enhanced top-HUD-band extension
    bool visual_background = false;   // PC1 levels skip column extension
    int glider_water_y = -1;          // enhanced icy sea baseline; -1 = off
    // tile_sprites->size() before the GROT3 append (the L3 trunk is at +0/+1).
    int surface_tile_count = 0;
    const prepare::LevelTiles* level_tiles = nullptr;  // authored placements
    // The final atlas the placements index (L3: + GROT3); read for its size and
    // the column patterns.
    const std::vector<formats::Sprite>* tile_sprites = nullptr;
};

// Build the render tile list for a surface screen into `out` (cleared):
// per-level backdrop rows, authored placements (resolve_sprite_idx), the L7
// ceiling bridge, the glider water, the HUD-band column extension.  Returns
// the number of leading backdrop tiles.
int build_screen_tiles(const ScreenTileContext& ctx, int screen,
                       std::vector<LevelRenderAssets::TileDraw>& out);

}  // namespace olduvai::presentation
