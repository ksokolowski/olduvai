// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen adjacent-screen peek — pure foundation (Task 1).
//
// Prehistorik is a flip-screen game: each 320×200 screen is a self-contained
// room.  On a wide display we cannot show MORE game world, but we CAN fill the
// side margins with a cosmetic PEEK of the horizontally-adjacent screens'
// background + terrain (no entities), where coherent; clean bezel otherwise.
// Gameplay is unchanged — the flip still happens at the real 320 edge.
//
// This header exposes two PURE pieces (no render-pipeline integration here,
// that is Task 2):
//   * widescreen_neighbors — the peek/bezel decision table (SDL-free).
//   * compose_widescreen   — pixel-copy wide-buffer assembler.

#pragma once

#include <cstdint>
#include <vector>

#include "presentation/game_render.hpp"  // FrameBuffer

namespace olduvai::presentation {

// Resolved horizontal-peek neighbors for the current screen.  -1 = bezel
// (no coherent neighbor → clean margin fill, never a janky seam).
struct PeekNeighbors {
    int left = -1;
    int right = -1;
};

// Decide which horizontally-adjacent SURFACE screens may be peeked into the
// margins for the current state.  -1 means "no coherent neighbor → bezel".
//
// Rules (§8.7):
//   * caves / secrets (current_screen >= 100) → bezel
//   * boss levels (internal 2/4/6 — single arena) → bezel
//   * ALL FOUR surface levels peek — internal 1 (jungle), 5 (icy), 3 (dark
//     woods), 7 (volcanic).  Peek composes the REAL adjacent screen
//     (compose_surface_screen_static), which works for the tile-based levels
//     (3/7) just as for the shared-FOND levels (1/5) — the neighbour is real
//     level content either way.  (An earlier assumption that 3/7 "have no shared
//     backdrop so must bezel" was WRONG: the shared FOND only matters for the
//     no-neighbour first/last-edge BACKDROP-EXTEND, not for neighbour peek.)
//   * the level's first / last surface edge (no neighbour) → caller fallback
//     (FOND backdrop-extend on 1/5; 3/7 have no FOND so that one edge differs)
//
// `surface_screen_count` = number of surface screens in this level (the caller
// knows it).  `secret_flag` is accepted for parity with the SystemsState gate
// even though current_screen >= 100 already covers the cave/secret case.
PeekNeighbors widescreen_neighbors(int internal_level, int current_screen,
                                   bool secret_flag, int surface_screen_count);

// Assemble a wide RGBA buffer (width = 320 + 2*margin, height = 200) from a
// center 320×200 frame and optional left/right 320×200 neighbor frames.
//   * Left margin  = the right `margin` columns of `left`.
//   * Right margin = the left  `margin` columns of `right`.
//   * For a NON-null neighbor only rows >= hud_rows are peeked — the HUD strip
//     is excluded so a neighbor's HUD never bleeds into the margin; rows below
//     hud_rows fall back to the same-screen edge-clamp.
//   * Missing neighbor (nullptr) → BACKGROUND-LAYER EXTENSION when `backdrop`
//     is provided, otherwise the legacy self-tile / mirror fallback:
//       - With a non-null `backdrop` (the pure HUD-erased FOND, 320×200 RGBA,
//         containing sky+mountains+clouds but NO foreground tiles/sprites): the
//         no-neighbour margin is filled in TWO bands.  The upper "sky/mountain"
//         band (rows above the ground band) samples the BACKDROP's edge column
//         (col 0 for the left margin, col 319 for the right) — so sky+mountains
//         extend to the screen edge with NO duplicated foreground (the palm is a
//         tile, not in the backdrop).  The bottom "ground band" (the last
//         kGroundBandRows rows) clamps the CENTER's edge column — the base
//         floor/grass is horizontally uniform and the palm trunk sits above/
//         inset, so the very-bottom edge column is ground, letting grass/dirt
//         reach the screen edge.
//       - With a null `backdrop` (secret rooms — they fill with a colour, not a
//         FOND): the legacy edge-clamp self-tile / mirror fallback is used for
//         ALL rows (unchanged — secret rooms stay as-is).
//   * For a NON-null neighbor only rows >= hud_rows are peeked (unchanged); the
//     neighbour-peek path is NOT affected by `backdrop`.
//   * Center is copied verbatim.
// `out` is sized (320 + 2*margin) * 200 * 4.  Pure pixel copy; no SDL.

// Rows (from the bottom) that the no-neighbour, backdrop-driven fill treats as
// the ground band (clamp the CENTER's edge column = floor/grass) rather than
// the sky/mountain band (sample the BACKDROP's edge column).  Tuned so L1/L5's
// base floor + grass strip is covered.  v1 heuristic — see the design note.
inline constexpr int kWideGroundBandRows = 48;

// Rows (from the bottom) voided to black on an IMPASSABLE dead-end margin
// (void_ground_*): just the chunky dirt/rock FLOOR, leaving the grass strip +
// forest above intact (so it reads as "just backdrop" like screen 0's left,
// not a floating shelf over a black box).
inline constexpr int kWideDeadendVoidRows = 24;

// `reflect_pure`: when true and a margin has no neighbour and no backdrop, the
// self-tile fill is a **pure reflection of the edge strip with NO void-scan**
// — source pixels that happen to be pure black stay black instead of being
// scanned past to the nearest non-void column.  Use this for boss arenas where
// the cave/arena wall is legitimately black: the void-scan would replace those
// black pixels with a coloured interior pixel, turning a black wall into a
// smear of the wrong colour.  Default false preserves the legacy behaviour
// (void-scan extends the nearest real band outward) for all existing callers.
//
// `margin_edge_brightness`: per-column darkening gradient on the MARGINS only.
// 1.0 (default) = no change.  When < 1.0 the margin fades from full brightness
// at the mirror line (inner edge, seamless with the centre) down to this factor
// at the outer screen edge — used by the boss arenas so the mirror-reflected
// margins recede into cave shadow and the reflection symmetry stops drawing the
// eye.  The centre 320 is never touched.
void compose_widescreen(std::vector<std::uint8_t>& out, int margin,
                        const FrameBuffer& center,
                        const FrameBuffer* left, const FrameBuffer* right,
                        int hud_rows = 0,
                        const FrameBuffer* backdrop = nullptr,
                        bool reflect_pure = false,
                        float margin_edge_brightness = 1.0f,
                        // No-backdrop SURFACE levels (dark woods / volcanic) set
                        // this to REPEAT the scene into the no-neighbour margin
                        // (torus-wrap) instead of the mirror self-tile.  Secret
                        // rooms + boss arenas leave it false (keep the mirror).
                        bool repeat_no_backdrop = false,
                        // When a `backdrop` is provided, also fill the GROUND
                        // band from the backdrop instead of mirroring the near
                        // floor — so the no-neighbour margin is PURE backdrop
                        // (sky+mountains), no foreground ground.  Used by L1's
                        // mid-air-island end screen, where the area beside the
                        // island should read as open sky, not extended ground.
                        bool ground_backdrop = false,
                        // IMPASSABLE dead-end edge (L3 trunk-entry screen 9
                        // right): void ONLY the bottom kWideDeadendVoidRows (the
                        // chunky dirt/rock floor) of the no-neighbour margin to
                        // black, per side — the grass + forest above still fill,
                        // matching the "just backdrop" look of screen 0's left
                        // (whose floor doesn't reach that edge).  Shallower than
                        // the full ground band so the grass strip isn't cut into
                        // a floating shelf.
                        bool void_ground_left = false,
                        bool void_ground_right = false);

}  // namespace olduvai::presentation
