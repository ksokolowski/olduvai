// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Vertical tile-column patterns (enhanced widescreen support).
//
// The game composes tall elements — Dark Woods forest trunks (spr 28 / spr 4
// stacks), the giant level-end trunk (spr 24 over spr 25), L7 rock pillars —
// as VERTICAL COLUMNS: tiles at the same x where each next tile starts exactly
// at prev.y + prev.height (the sprite may vary within a column).  The
// pattern-matching spike (internal notes) validated that
// these columns are cleanly detectable from the placements alone and proposed
// one shared primitive to replace the three divergent ad-hoc detectors that
// had accreted (extend_top_trunk_columns, collect_seam_trunks, the L3 tt pass
// in compose_static_wide_bg_native).  This module is that primitive:
//
//   * extend_columns_to_top — continue a column whose top lands in the HUD
//     band up to row 0 (repeat the topmost tile at its own height stride), so
//     trunks/pillars don't read "cut off" under the transparent HUD.
//   * seam_straddling_tiles — collect a screen's tiles that STRADDLE one of
//     its vertical edges (x=0 / x=320), so a neighbour's seam trunk can be
//     completed whole into the adjacent widescreen margin instead of being
//     clipped at the screen boundary.
//
// Pure placement-list operations — no SDL, no rendering, no game state.

#pragma once

#include <vector>

#include "formats/mat.hpp"
#include "presentation/game_render.hpp"

namespace olduvai::presentation::tile_patterns {

using TileDraw = LevelRenderAssets::TileDraw;

// Columns whose top tile lies in (0, top_band] are continued up to row 0 by
// repeating the TOPMOST tile at its own height stride (handles both uniform
// stacks and mixed-sprite columns like the giant trunk's 24-over-25 — the
// repeat reuses whichever sprite tops the column, matching how the game
// authors the taller instances of the same trunk).  Lone tiles (no chain
// neighbour above or below) are left alone: bushes, platforms, signs.
void extend_columns_to_top(std::vector<TileDraw>& tiles,
                           const std::vector<formats::Sprite>& sprites,
                           int top_band = 16);

// Collect ALL tiles that straddle the screen's RIGHT edge (right_edge=true:
// x < 320 && x + w > 320) or LEFT edge (right_edge=false: x < 0 && x + w > 0)
// — trunks, bushes, beams alike: anything authored across the boundary is cut
// by the native clip and must be completed on the other side (the S16 foliage
// at x=256, w=80 is a LONE tile; an earlier column-only gate left it cut at
// S17's seam).  Returned in the screen's own coordinates; the caller re-blits
// them at a ±320 offset, and the layering laws (own-slot exclusion, authored
// content redrawn on top, player box last) keep the overhang harmless.
std::vector<TileDraw> seam_straddling_tiles(
    const std::vector<TileDraw>& tiles,
    const std::vector<formats::Sprite>& sprites, bool right_edge);

// Bridge an AUTHORED seam hole in a contiguous decor row: screen A (left of
// the seam) authors a row of sprite P at stride == width, stops short of
// x=320, and screen B resumes the SAME sprite at the SAME y just past the
// seam — flip-screen DOS never shows both sides at once, so the hole was
// invisible; the widescreen peek exposes it (the L7 S1|S2 jumppad rail,
// spr 22 at y=97: A ends at 304, B resumes at 320 — a 16px hole floating
// mid-view).  Returns the fill tiles in SCREEN-A coordinates (x >= 320-2w,
// possibly past 320); the caller re-expresses them for its seam list.
// Gates (all required — bridging a WALKABLE gap would paint ground over an
// authored jump pit):
//   * same sprite index and y on both sides, gap <= 3 tile widths;
//   * at least one side is a CONTIGUOUS run (>= 2 tiles at stride == w) —
//     spaced features (fence posts, doors) keep their natural spacing;
//   * the hole is not covered by ANY other tile of either screen;
//   * the row bottom is above the ground band (y + h <= 150) — every
//     walkable seam gap found in the data (L1/L3/L5 ground rows, jump
//     pits) sits below that line.
// a_backdrop / b_backdrop = each list's LevelRenderAssets::backdrop_tile_count
// (the bind-injected backdrop/level split): backdrop tiles neither author rows
// nor count as coverage — the lavarock wall behind the rail IS the visible
// hole, not a filler.
std::vector<TileDraw> seam_row_bridges(
    const std::vector<TileDraw>& a_tiles, int a_backdrop,
    const std::vector<TileDraw>& b_tiles, int b_backdrop,
    const std::vector<formats::Sprite>& sprites);

}  // namespace olduvai::presentation::tile_patterns
