// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Smooth-motion previous-tick snapshot.  Copies every logic position the render
// interpolation reads into its prev_* shadow BEFORE the sim tick (mirrors the
// reference save_prev_positions).  Presentation-only: the prev_* fields are
// render-interp shadows, NOT in the per-frame JSONL trace, so relocating these
// copies is byte-identical.  MUST run before run_frame each tick.
#pragma once

#include <cmath>
#include <cstdlib>

#include "systems/player.hpp"   // systems::SystemsState

namespace olduvai::presentation {

// ── The other half of smooth motion: the interpolation itself ───────────────
//
// The snapshot above says WHAT to remember; these say how to read it back.
// They live together because they are twins — a prev_* field nothing
// interpolates is dead weight, and an interpolation with no snapshot reads
// garbage.
//
// THE SNAP GUARD.  A position that moved more than this in one logic tick did
// not travel, it TELEPORTED (screen change, respawn, warp), and interpolating
// across a teleport smears the sprite over the whole screen.  Above the
// threshold the render jumps straight to the new position.
//
// The constant was `constexpr int kSnap = 16;` in BOTH game_app.cpp and
// boss_app.cpp, carrying the same `// reference _SNAP_THRESHOLD` comment, with
// a float twin `systems::kSnapThreshold` for the bubbles.  Three spellings of
// one number is exactly the shape CONTRIBUTING's helper-ownership rule names.
inline constexpr int kSnapPx = 16;   // reference _SNAP_THRESHOLD

// Did this axis teleport rather than move?
//
// `force` is the caller's own snap SIGNAL, independent of distance: the
// platform loop passes its screen/cave-mode change, because entering the L3
// screen-4 cave moves the player only (9,11) px — UNDER the threshold — and
// interpolating that swept the sprite across the screen (bug report
// 2026-07-17_165051_L3_S4, gated by `cave_lerp`).  Distance alone cannot see
// that; only the caller knows.  Bosses have no screen changes and pass nothing.
inline bool snap_jumped(int prev, int cur, bool force = false) {
    return force || std::abs(cur - prev) > kSnapPx;
}

// Interpolated position for one axis, as a float — the sub-pixel form the HD
// path wants (see RenderTarget::player_fx/fy).  Teleports return `cur`.
inline float snap_lerp_f(int prev, int cur, float alpha, bool force = false) {
    if (snap_jumped(prev, cur, force)) return static_cast<float>(cur);
    return static_cast<float>(prev) + static_cast<float>(cur - prev) * alpha;
}

// Integer twin, for the logic-position shadow a renderer reads when it has no
// float path.  Rounds the same way the callers always did (lround).
inline int snap_lerp_i(int prev, int cur, float alpha, bool force = false) {
    if (snap_jumped(prev, cur, force)) return cur;
    return prev + static_cast<int>(
                      std::lround(static_cast<double>(cur - prev) * alpha));
}

// ── The pair form, and why it returns BOTH shadows from one call ────────────
//
// A pair guards on EITHER axis and then snaps BOTH, so a diagonal teleport
// cannot half-interpolate — that is a real rule, not a spelling, and it is why
// two scalar calls are NOT a substitute.
//
// It returns the integer AND float shadows together because every caller needs
// both and they must agree.  Open-coded, they were two `if` statements with
// the same condition written twice — and a duplicated decision is a decision
// that can diverge.  The L6 victory drop shipped broken for the neighbouring
// reason (a lerp whose render half never happened), so here the two halves
// cannot be computed apart: one branch, one result.
struct SnapLerp2 {
    int x, y;        // integer logic shadow
    float fx, fy;    // sub-pixel render shadow (RenderTarget::player_fx/fy)
};

inline SnapLerp2 snap_lerp_pair(int prevx, int prevy, int curx, int cury,
                                float alpha, bool force = false) {
    if (force || snap_jumped(prevx, curx) || snap_jumped(prevy, cury)) {
        return {curx, cury, static_cast<float>(curx), static_cast<float>(cury)};
    }
    return {snap_lerp_i(prevx, curx, alpha), snap_lerp_i(prevy, cury, alpha),
            snap_lerp_f(prevx, curx, alpha), snap_lerp_f(prevy, cury, alpha)};
}

inline void save_prev_positions(systems::SystemsState& s) {
    s.player.prev_x = s.player.x;
    s.player.prev_y = s.player.y;
    s.player.prev_dx = s.player.dx;
    s.player.prev_dy = s.player.dy;
    for (auto& e : s.entities) {
        e.prev_x = e.x;
        e.prev_y = e.y;
        e.prev_current_y = e.current_y;
        e.prev_throw_x = e.throw_x;
        e.prev_throw_y = e.throw_y;
        e.prev_draw_dy = e.draw_dy;
    }
    s.prev_stone_x = s.stone_x;
    s.prev_stone_y = s.stone_y;
    s.prev_fireball_x = s.fireball_x;
    s.prev_fireball_y = s.fireball_y;
    s.prev_glider_x = s.glider_x;
    s.prev_glider_y = s.glider_y;
    s.prev_death_halo_x = s.death_halo_x;
    s.prev_death_halo_y = s.death_halo_y;
    for (auto& b : s.score_bonuses) {
        b.prev_x = b.x;
        b.prev_y = b.y;
    }
}

}  // namespace olduvai::presentation
