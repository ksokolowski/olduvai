// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// What a no-neighbour widescreen margin looks like, per level and screen (the
// level's first/last screens, cave-hall outer seams).  Policy only; the
// compositor paints.  One pure function so each rule can be read and tested
// by name (tests/test_edge_margin_policy.cpp).  No SDL, no framebuffers.
#pragma once

#include "core/constants.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

// The margin decisions for one composed screen.
struct EdgeMarginPolicy {
    // L1 island end screen: the margin is open sky, filled from the FOND
    // backdrop only, not the extended ground.
    bool sky_only = false;

    // Same screen: continue the lake water into the right margin and the centre
    // void past the island (after the centre overlay).
    bool water_continues = false;

    // L3 screens 9 and 17, right edge: an impassable dead end (player clamped
    // at x=270; the trunk is entered going down).  Black out the dirt band so
    // the strip does not offer a ledge to nowhere.
    bool void_ground_right = false;

    // Tile levels (no FOND): start the margin black, then the layer extension
    // continues only authored patterns (backdrop rows, floors, walls).  A
    // mirror would clone edge objects (S13's half door at x=68).
    bool black_base = false;

    // Do not redraw the screen's tiles into the margin:
    //   * L1 island end: the margin is already pure backdrop; the extension
    //     would draw the island's ground and palm into the sky;
    //   * L7 cave hall (screens 10-12): the outer seams (warps to S9 / S13)
    //     stay pure black, like cave interiors (impassable).
    bool skip_tile_extension = false;

    // Surface levels without a backdrop (dark woods, volcanic): the sky/tree
    // band continues from the screen's own columns, the ground band mirrors the
    // floor. Secret rooms keep the self-tile.
    bool repeat_no_backdrop = false;
};

// The policy for the screen `state` is on.  `has_backdrop`: a FOND buffer was
// passed (null if unused or failed to build).  `screen_uses_backdrop`: the
// current screen's flag.  The black base keys off the flag, so a missing FOND
// falls back to the mirror instead of black.  `has_left` / `has_right`: that
// side has a neighbour (false = level edge).
inline EdgeMarginPolicy edge_margin_policy(const systems::SystemsState& state,
                                           bool has_backdrop,
                                           bool screen_uses_backdrop,
                                           bool has_left, bool has_right) {
    (void)has_left;   // no rule keys off the left edge today; see sky_only
    EdgeMarginPolicy p;

    // The level-complete handler moves current_screen to kLastScreen + 1 (the
    // pseudo-exit) before the fade composes; accept both.
    const bool l1_end = state.current_level == 1 &&
                        (state.current_screen == core::kLastScreen ||
                         state.current_screen == core::kLastScreen + 1);

    // L7 lava cave-hall: screens 10-12, the closed-cave case above.
    const bool l7_cave_hall = state.current_level == 7 &&
                              state.current_screen >= 10 &&
                              state.current_screen <= 12;

    p.sky_only = l1_end && has_backdrop;
    p.water_continues = l1_end && !has_right;
    p.skip_tile_extension = l1_end || l7_cave_hall;
    p.void_ground_right =
        state.current_level == 3 && !has_right &&
        (state.current_screen == 9 || state.current_screen == 17);
    p.black_base = !screen_uses_backdrop && state.secret_flag == 0 &&
                   state.current_screen < 100;
    p.repeat_no_backdrop = state.secret_flag == 0;
    return p;
}

}  // namespace olduvai::presentation
