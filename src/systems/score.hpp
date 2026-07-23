// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Score subsystem: accumulation (999,999 cap + extra-life-per-10K) and the
// 10-slot score popups (draw-before-move ordering).  Moved out of
// collision_dispatch so that TU is the collision-result glue, not also the
// scoring model (SOC roadmap: score).  collision_dispatch.hpp re-includes
// this, so existing callers are unaffected.
//                                              // FUN_27f7_13d4 / FUN_27f7_0798
#pragma once

#include "systems/player.hpp"   // SystemsState

namespace olduvai::systems {

constexpr int kScoreCap = 999999;
constexpr int kScoreSpriteBase = 33;
constexpr int kScorePopupFrames = 10;
constexpr int kScorePopupDy = -4;

void add_score(SystemsState& state, int amount);
void add_score_popup(SystemsState& state, int x, int y, int score);
void update_score_bonuses(SystemsState& state);  // pre-render decrement
void move_score_bonuses(SystemsState& state);    // post-render y -= 4

}  // namespace olduvai::systems
