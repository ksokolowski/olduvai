// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Vertical tile-column patterns (enhanced widescreen).  Tall elements (Dark
// Woods trunks, the giant level-end trunk 24-over-25, L7 pillars) are authored
// as columns: tiles at one x, each starting at prev.y + prev.height.
//   * extend_columns_to_top: continue a column whose top is in the HUD band up
//     to row 0, so trunks are not cut off under the transparent HUD.
//   * seam_straddling_tiles / seam_row_bridges: complete objects across a
//     screen seam in the widescreen margins.
// Pure placement-list operations.

#pragma once

#include <vector>

#include "formats/mat.hpp"
#include "presentation/render/game_render.hpp"

namespace olduvai::presentation::tile_patterns {

using TileDraw = LevelRenderAssets::TileDraw;

// Columns whose top tile lies in (0, top_band] continue to row 0 by repeating
// the topmost tile at its height stride (whatever sprite tops the column).
// Lone tiles (bushes, platforms, signs) are left alone.
void extend_columns_to_top(std::vector<TileDraw>& tiles,
                           const std::vector<formats::Sprite>& sprites,
                           int top_band = 16);

// All tiles straddling the right edge (x < 320 < x + w) or left edge (x < 0 <
// x + w): anything across the boundary is clipped and must be completed on the
// other side (S16's lone foliage tile at x=256, w=80).  Screen coordinates; the
// caller re-blits at +-320 under the seam layering rules.
std::vector<TileDraw> seam_straddling_tiles(
    const std::vector<TileDraw>& tiles,
    const std::vector<formats::Sprite>& sprites, bool right_edge);

// Bridge an authored seam hole in a decor row: screen A's run of sprite P
// (stride = width) stops short of 320 and screen B resumes it at the same y
// (the L7 S1|S2 jumppad rail, spr 22 at y=97: a 16 px hole the widescreen peek
// exposes).  Returns fill tiles in A coordinates.  All gates required, so a
// walkable pit is never paved over:
//   * same sprite and y on both sides, gap <= 3 tile widths;
//   * one side is a contiguous run (>= 2 tiles at stride w);
//   * no other tile of either screen covers the hole;
//   * the row ends above the ground band (y + h <= 150).
// a_backdrop / b_backdrop: each list's backdrop_tile_count; backdrop tiles
// neither form rows nor count as coverage.
std::vector<TileDraw> seam_row_bridges(
    const std::vector<TileDraw>& a_tiles, int a_backdrop,
    const std::vector<TileDraw>& b_tiles, int b_backdrop,
    const std::vector<formats::Sprite>& sprites);

}  // namespace olduvai::presentation::tile_patterns
