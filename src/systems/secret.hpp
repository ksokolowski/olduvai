// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Secret areas — underwater bonus rooms entered by falling through floor
// traps; exit by jumping high (y < 30 while ascending).  // L1 main secret
// block; per-frame room render FUN_27f7_01ff
//
// L1 entries: screen 5 (x<105, y>155) → secret 0, return (5,100,70);
// screen 14 → secret 1, return (14,80,100).  Exit dampens y_vel (secret 0:
// >>2 if <0x7F else 0x22; secret 1: always >>2).  A trampoline at the left
// wall (x<20, y==139) bounces: y-=20, y_vel = min(y_vel<<2, 0x7F).

#pragma once

#include "systems/player.hpp"

namespace olduvai::systems {

constexpr int kSecretFloorY = 168;

bool check_secret_entry(SystemsState& state);
void check_secret_exit(SystemsState& state);
void setup_secret_collision(SystemsState& state);
// Returns true when the bounce fired (renderer draws the raised spring).
bool update_secret_trampoline(SystemsState& state);

// Balloon/glider flight physics — directional flight on screens 10/11
// (L1 balloon, L5 glider); saves the restart point every flight frame
// with the hardcoded y=100.                       // FUN_27f7_1af4
void update_flight_physics(SystemsState& state);

// Hide the decorative BALLOONS entities once flight begins (shared
// dispatcher slot for L1 balloon + L5 glider scenery).
void sync_balloon_visibility(SystemsState& state);

}  // namespace olduvai::systems
