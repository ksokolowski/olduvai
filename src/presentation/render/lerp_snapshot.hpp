// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Smooth-motion previous-tick snapshot: copy every logic position the render
// interpolation reads into its prev_* shadow, before run_frame each tick.
// Render-only (prev_* are not traced).
#pragma once

#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include "systems/player.hpp"   // systems::SystemsState

namespace olduvai::presentation {

// ---- The interpolation that reads the snapshot back ----
// A move of more than kSnapPx in one tick is a teleport (screen change,
// respawn, warp); interpolating it would smear the sprite, so the render
// jumps.
inline constexpr int kSnapPx = 16;   // reference _SNAP_THRESHOLD

// Did this axis teleport rather than move?  `force` is the caller's own signal:
// the platform loop passes a screen/cave-mode change (the L3 screen-4 cave
// entry moves only 9,11 px).  Bosses pass nothing.
inline bool snap_jumped(int prev, int cur, bool force = false) {
    return force || std::abs(cur - prev) > kSnapPx;
}

// Interpolated position for one axis as a float (the HD sub-pixel form);
// teleports return `cur`.
inline float snap_lerp_f(int prev, int cur, float alpha, bool force = false) {
    if (snap_jumped(prev, cur, force)) return static_cast<float>(cur);
    return static_cast<float>(prev) + static_cast<float>(cur - prev) * alpha;
}

// Integer twin (lround), for renderers without a float path.
inline int snap_lerp_i(int prev, int cur, float alpha, bool force = false) {
    if (snap_jumped(prev, cur, force)) return cur;
    return prev + static_cast<int>(
                      std::lround(static_cast<double>(cur - prev) * alpha));
}

// ---- The pair form ----
// Guards on either axis and snaps both, so a diagonal teleport cannot
// half-interpolate.  Returns the integer and float shadows from one decision
// so they always agree.
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

// The logic positions the smooth sub-frames overwrite: saved before the fill,
// the interpolation target, and restored after it.
struct LogicPositions {
    struct Ent { int x, y, cy, tx, ty, ddy; };
    systems::PlayerState player;
    std::vector<Ent> entities;
    int stone_x = 0, stone_y = 0;
    int fireball_x = 0, fireball_y = 0;
    int glider_x = 0, glider_y = 0;
    int death_halo_x = 0, death_halo_y = 0;
    std::array<std::pair<int, int>, 10> bonus{};
};

inline LogicPositions save_logic_positions(const systems::SystemsState& s) {
    LogicPositions p;
    p.player = s.player;
    p.entities.reserve(s.entities.size());
    for (const auto& e : s.entities)
        p.entities.push_back(
            {e.x, e.y, e.current_y, e.throw_x, e.throw_y, e.draw_dy});
    p.stone_x = s.stone_x;
    p.stone_y = s.stone_y;
    p.fireball_x = s.fireball_x;
    p.fireball_y = s.fireball_y;
    p.glider_x = s.glider_x;
    p.glider_y = s.glider_y;
    p.death_halo_x = s.death_halo_x;
    p.death_halo_y = s.death_halo_y;
    for (std::size_t i = 0; i < s.score_bonuses.size(); ++i)
        p.bonus[i] = {s.score_bonuses[i].x, s.score_bonuses[i].y};
    return p;
}

inline void restore_logic_positions(systems::SystemsState& s,
                                    const LogicPositions& p) {
    s.player = p.player;
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        auto& e = s.entities[i];
        const auto& sv = p.entities[i];
        e.x = sv.x;
        e.y = sv.y;
        e.current_y = sv.cy;
        e.throw_x = sv.tx;
        e.throw_y = sv.ty;
        e.draw_dy = sv.ddy;
    }
    s.stone_x = p.stone_x;
    s.stone_y = p.stone_y;
    s.fireball_x = p.fireball_x;
    s.fireball_y = p.fireball_y;
    s.glider_x = p.glider_x;
    s.glider_y = p.glider_y;
    s.death_halo_x = p.death_halo_x;
    s.death_halo_y = p.death_halo_y;
    for (std::size_t i = 0; i < s.score_bonuses.size(); ++i) {
        s.score_bonuses[i].x = p.bonus[i].first;
        s.score_bonuses[i].y = p.bonus[i].second;
    }
}

// Set every interpolated field to `alpha` between its prev_* value and `cur`,
// the int and float shadows from one guarded decision: player x/y (dx/dy only
// during the ghost rise), entity x/y/current_y/throw/draw_dy, fireball,
// glider, death halo, rolling stone, score popups.  `force` snaps them all (a
// screen or cave/secret mode change can be a hop under kSnapPx).  The player's
// float position goes to player_fx/fy: it lives on RenderTarget.
inline void apply_interpolated(systems::SystemsState& s,
                               const LogicPositions& cur, float alpha,
                               bool force, float& player_fx,
                               float& player_fy) {
    auto lerp2 = [&](int& x, int& y, float& fx, float& fy, int prevx,
                     int prevy, int curx, int cury) {
        const auto r = snap_lerp_pair(prevx, prevy, curx, cury, alpha, force);
        x = r.x;
        y = r.y;
        fx = r.fx;
        fy = r.fy;
    };
    auto lerp1 = [&](int& v, float& fv, int prevv, int curv) {
        v = snap_lerp_i(prevv, curv, alpha, force);
        fv = snap_lerp_f(prevv, curv, alpha, force);
    };
    const systems::PlayerState& p = cur.player;
    lerp2(s.player.x, s.player.y, player_fx, player_fy, p.prev_x, p.prev_y,
          p.x, p.y);
    // dx/dy carry the gait offset (added to a float base, so int only);
    // lerping them staggers the walk.  Only during the ghost rise.
    if (s.player.ghost_rise != 0) {
        const auto r = snap_lerp_pair(p.prev_dx, p.prev_dy, p.dx, p.dy, alpha,
                                      force);
        s.player.dx = r.x;
        s.player.dy = r.y;
    }
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        auto& e = s.entities[i];
        const auto& sv = cur.entities[i];
        lerp2(e.x, e.y, e.fx, e.fy, e.prev_x, e.prev_y, sv.x, sv.y);
        lerp1(e.current_y, e.f_current_y, e.prev_current_y, sv.cy);
        lerp2(e.throw_x, e.throw_y, e.f_throw_x, e.f_throw_y, e.prev_throw_x,
              e.prev_throw_y, sv.tx, sv.ty);
        lerp1(e.draw_dy, e.f_draw_dy, e.prev_draw_dy, sv.ddy);
    }
    if (s.fireball_flag != 0)
        lerp2(s.fireball_x, s.fireball_y, s.fireball_fx, s.fireball_fy,
              s.prev_fireball_x, s.prev_fireball_y, cur.fireball_x,
              cur.fireball_y);
    lerp2(s.glider_x, s.glider_y, s.glider_fx, s.glider_fy, s.prev_glider_x,
          s.prev_glider_y, cur.glider_x, cur.glider_y);
    if (s.death_halo_active)
        lerp2(s.death_halo_x, s.death_halo_y, s.death_halo_fx,
              s.death_halo_fy, s.prev_death_halo_x, s.prev_death_halo_y,
              cur.death_halo_x, cur.death_halo_y);
    if (s.stone_state != 0)
        lerp2(s.stone_x, s.stone_y, s.stone_fx, s.stone_fy, s.prev_stone_x,
              s.prev_stone_y, cur.stone_x, cur.stone_y);
    for (std::size_t i = 0; i < s.score_bonuses.size(); ++i) {
        auto& b = s.score_bonuses[i];
        if (b.counter > 0)
            lerp2(b.x, b.y, b.fx, b.fy, b.prev_x, b.prev_y, cur.bonus[i].first,
                  cur.bonus[i].second);
    }
}

}  // namespace olduvai::presentation
