// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Smooth-motion previous-tick snapshot.  Copies every logic position the render
// interpolation reads into its prev_* shadow BEFORE the sim tick (mirrors the
// reference save_prev_positions).  Presentation-only: the prev_* fields are
// render-interp shadows, NOT in the per-frame JSONL trace, so relocating these
// copies is byte-identical.  MUST run before run_frame each tick.
#pragma once

#include "systems/player.hpp"   // systems::SystemsState

namespace olduvai::presentation {

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
