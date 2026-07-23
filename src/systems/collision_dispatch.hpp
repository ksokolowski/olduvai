// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Collision-result application + bonus dispatch + score + fireball — the
// glue between the collision pass and game state.
//
// Bonus_Activate 6-entry dispatch:                  // FUN_27f7_1202
//   0 spring (y_vel := 0x22) · 1 bomb (kill-all + palette flash) ·
//   2 timer +30 clamp 99 + HUD flash · 3 life++ clamp 99 ·
//   4 shield (hit_counter := 99) · 5 axe (axe-powered flag)
// Score accumulation (cap 999,999; extra life per 10K):  // FUN_27f7_13d4
// Score popups (10 slots; draw-before-move ordering):    // FUN_27f7_0798
// Fireball flight (±8 px/frame, despawn off-screen):     // Game_UpdateFireball

#pragma once

#include "systems/collisions.hpp"
#include "systems/player.hpp"
#include "systems/score.hpp"

namespace olduvai::systems {

constexpr int kBombFlashFrames = 20;     // B/W palette-cycle duration
constexpr int kCaveExitOffset = 8;       // cave_exit_x = width - offset

void dispatch_bonus_activate(SystemsState& state, int bt);
void update_fireball(SystemsState& state);
// Cave-sign teleport state writes (EXE seg:0da6) — shared by the classic
// immediate path and the Enhanced #20 deferred completion.
void apply_sign_teleport(SystemsState& state, int screen, int x, int y);

// Enhanced #20 — complete a DEFERRED cave-sign teleport once the departure
// clouds have finished (teleport_out_ticks == 0), arming the arrival
// sequence.  Returns true when it applied.  MUST run at the logic step of a
// tick — after the game loop's pre-frame snapshot and before run_frame — so
// the transition classifier sees the cave→surface edge and plays the cave
// fade pair.  Completing it in the end-of-tick presentation block mutated
// cave_flag/current_screen outside the snapshot bracket, and the next tick
// misclassified the change as a surface pan-scroll (2026-07-06 regression).
bool try_complete_sign_teleport(SystemsState& state);

void process_entity_collisions(SystemsState& state);

}  // namespace olduvai::systems
