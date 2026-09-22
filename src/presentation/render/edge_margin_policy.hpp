// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// What a NO-NEIGHBOUR widescreen margin should look like, per level and screen.
//
// A widescreen margin normally peeks the neighbouring screen.  The level's
// FIRST screen has no left neighbour and its LAST has no right one, and a cave
// hall's outer seams have neither — so those margins must be invented, and
// what looks right there is not one rule but several, each decided by LOOKING
// at the result and each owing its shape to a visual defect it fixed.
//
// Those decisions used to be five booleans computed inline in the middle of
// `compose_static_wide_bg_native`, fanned out to three different consumers
// (the `MarginFill` passed to compose_widescreen, the three flags passed to
// fill_no_neighbour_margin, and a conditional call to continue_l1_end_water).
// They are policy — WHICH enhancement applies to this screen — while all three
// consumers are mechanism — HOW the pixels get painted.  This header is the
// policy, in one pure function, so the rules can be read, tested and changed
// without touching a compositor, and so a refactor of the pixel work cannot
// quietly drop one of them (tests/test_edge_margin_policy.cpp asserts each
// rule by name; before it, a lost rule showed up only as a changed golden).
//
// Pure: no SDL, no framebuffers, no state written.  Inputs are the level and
// screen, whether each side HAS a neighbour, and whether this level draws a
// FOND backdrop.
#pragma once

#include "core/constants.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

// The margin decisions for one composed screen.  Every field is a rule that
// was chosen by eye and kept; the comment on each says what it fixes.
struct EdgeMarginPolicy {
    // L1 (jungle) mid-air-island END screen: the area beside the island reads
    // as OPEN SKY, so a no-neighbour margin is filled from the FOND backdrop
    // only (sky + mountains) and NOT from the extended foreground ground.
    // Only this screen — the first and middle screens keep the ground
    // extension, which is what looks right there.
    bool sky_only = false;

    // The same screen's lake: continue the water into the right margin (and
    // the centre's bottom-right void past the island).  Runs after the centre
    // overlay, which would otherwise clobber the void part.
    bool water_continues = false;

    // L3 trunk-entry screens 9 and 17, RIGHT edge: an impassable dead-end (the
    // player is clamped at x=270; the trunk is entered going DOWN, never by
    // walking right).  Its full-width floor reaches the edge, so mirroring
    // would offer a walkable dirt ledge to nowhere — void just that dirt band
    // so the strip reads as "just backdrop", like screen 0's left whose floor
    // does not reach the edge either.
    bool void_ground_right = false;

    // Tile-based surface levels (no FOND backdrop): a no-neighbour margin gets
    // a BLACK BASE before the tile layer-extension, instead of
    // compose_widescreen's self-tile MIRROR of the screen's edge columns.  The
    // mirror cloned edge OBJECTS into the margin — a half-door authored at
    // S13's x=68 reflected as a phantom door sliver, mostly-black cave-hall
    // edges smeared — while the layer extension rebuilds the margin from the
    // authored PATTERNS only: backdrop rows, floor bands and wall columns
    // continue, and everything unauthored stays dark.
    bool black_base = false;

    // Do NOT re-draw the screen's tiles into a no-neighbour margin.  Two
    // different screens want this, which is why it is an EFFECT here and not
    // a level name:
    //   * the L1 island end screen — compose_widescreen has already filled
    //     the margin with pure backdrop, and the tile extension would draw
    //     the island's ground and palm back into the open sky;
    //   * the L7 lava cave-hall (screens 10-12), whose outer seams (S10 left
    //     = the S9 cave-descent warp, S12 right = the S13 teleport warp) stay
    //     PURE BLACK like the regular cave interiors — owner-picked after
    //     seeing the darkness-rows and gray-wall alternatives, because those
    //     warp boundaries are impassable and the hall should read as closed.
    bool skip_tile_extension = false;

    // Surface levels without a backdrop (dark woods / volcanic): the sky /
    // tree band CONTINUES from the screen's own columns while the GROUND band
    // mirrors the near floor.  Secret rooms keep their deliberate self-tile
    // instead.
    bool repeat_no_backdrop = false;
};

// Decide the policy for the screen `state` is on.
//
// The two backdrop inputs are NOT the same question, and conflating them is a
// real defect rather than a tidy-up: `has_backdrop` is whether a FOND buffer
// was handed to the compositor (it can be null because the screen does not
// use one OR because it failed to build), while `screen_uses_backdrop` is the
// CURRENT SCREEN's own flag (`render->visual_background`).  A screen that
// wants a FOND but has no buffer must not be treated as a tile level: the
// original keyed the black base off the screen flag, and deriving it from the
// pointer would paint the margin black for a missing-asset case that used to
// fall back to the mirror.
//
// `has_left` / `has_right` are whether that side has a neighbour to peek
// (false = this is the level's edge).
inline EdgeMarginPolicy edge_margin_policy(const systems::SystemsState& state,
                                           bool has_backdrop,
                                           bool screen_uses_backdrop,
                                           bool has_left, bool has_right) {
    (void)has_left;   // no rule keys off the left edge today; see sky_only
    EdgeMarginPolicy p;

    // current_screen is kLastScreen during play, but the level-complete
    // handler bumps it to kLastScreen + 1 (the pseudo-exit) the instant
    // level_complete fires, and the level-end fade composes from THAT state.
    // Accept both, so the water margin survives into the fade's first frame
    // instead of reverting to the mirror fallback.
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
