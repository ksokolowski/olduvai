// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Screen-boundary transitions (L1 jungle handler — others land with their
// levels).  // FUN_21f3_006f screen-edge block
//
// L1: left edge x<6; screen-9 balloon-ride entry (x>260) / exit (x>299);
// normal right x>295 except screens 9 and 18; food gate on screen 18
// (x>260 with food >= 45 → level complete).

#pragma once

#include "systems/player.hpp"

namespace olduvai::systems {

constexpr int kFoodGate = 45;

void check_l1_transition(SystemsState& state);
void check_l3_transition(SystemsState& state);
void check_l5_transition(SystemsState& state);
void check_l7_transition(SystemsState& state);
// Dispatch by current_level (1/3/5/7).
void check_screen_transition(SystemsState& state);

// Pre-roll the L3 trunk-descent smoke Y jitter from the GLOBAL LCG into
// state.l3_descent_smoke_jitter.  Must be called logic-side (before any
// blocking animation) so the 40 LCG draws are consumed in the correct order
// regardless of replay/headless/enhanced mode — mirrors reference 789541c.
// FUN_2276_03d9:0x0554 (smoke A) + 0x0586 (smoke B), iters 0..19.
void roll_l3_descent_smoke_jitter(SystemsState& state);
// L3/L7 cave-warp >>2 animation + the freeze==1000 teleports.
void check_cave_warp_animation(SystemsState& state);
// L5 glider grabs: screen 9 (x>215, y<20) and screen 18 (x>140, y<3,
// food past the gate); both set freeze frames at the grab.
void check_l5_glider_entry(SystemsState& state);
// L5 screen 12: detach the glider and animate it flying away.
void handle_l5_screen12_glider(SystemsState& state);

// DS slots every surface level zeroes at the top of its screen loop.
void clear_per_screen_state(SystemsState& state);

}  // namespace olduvai::systems
