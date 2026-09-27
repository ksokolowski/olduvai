// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Cave system: entry/exit, collision, boundaries.  Dispatcher FUN_2759_0645;
// per-level inits FUN_2759_027a/04ee/0428/033a.
// Entry: a descent animation driven by cave_entrance_mask (bits 1:0 frame,
// 7:2 cave index), then current_screen = 100 + cave_index.  Exit: walk left
// past x=6 (L1/L5/L7) or the cave-sign teleport.  counter == 1000 marks the L7
// screen-transition entrance.

#pragma once

#include "systems/player.hpp"

namespace olduvai::systems {

constexpr int kCaveSpawnY = 139;
constexpr int kSprCaveDescent1 = 44;

// Cave-emerge reveal length, armed by exit_cave.  Enhanced: 3 dim stages x 3
// ticks, player frozen (frame_runner).  Classic: 2 lit ticks, draw only.
constexpr int kCaveEmergeTicksEnhanced = 9;
constexpr int kCaveEmergeTicksClassic = 2;
constexpr int kCaveEmergeStageHold = 3;   // ticks per dim stage (enhanced)

// Cave palettes (16 RGB per level): hand-authored approximations of the cave
// tint (the original sets its own at entry, FUN_2759_00b7).  Project values,
// absent from the executable (byte-searched in both 6-bit encodings).
struct CaveRgb { int r, g, b; };
constexpr CaveRgb kCavePaletteL1[16] = {
    {0,0,0}, {162,97,65}, {130,65,32}, {97,32,0}, {0,0,0}, {162,0,32},
    {227,195,32}, {0,0,0}, {0,130,65}, {32,162,97}, {0,0,0}, {32,32,32},
    {65,65,65}, {97,97,97}, {130,130,130}, {195,195,195},
};
constexpr CaveRgb kCavePaletteL3[16] = {
    {0,0,0}, {162,97,65}, {130,65,32}, {97,32,0}, {32,32,0}, {162,0,32},
    {227,195,32}, {65,32,0}, {97,65,32}, {130,97,65}, {0,0,0}, {65,65,65},
    {97,97,97}, {162,130,97}, {162,162,162}, {227,227,227},
};
constexpr CaveRgb kCavePaletteL5[16] = {
    {0,0,0}, {162,97,65}, {130,65,32}, {97,32,0}, {32,32,97}, {162,0,32},
    {227,162,32}, {65,65,130}, {97,97,162}, {32,65,130}, {0,0,0},
    {32,32,32}, {97,97,97}, {130,130,195}, {162,162,195}, {227,227,227},
};
constexpr CaveRgb kCavePaletteL7[16] = {
    {0,0,0}, {162,97,65}, {130,65,32}, {97,32,0}, {0,0,0}, {97,0,32},
    {227,195,32}, {0,0,0}, {32,32,65}, {65,0,65}, {0,0,0}, {65,65,65},
    {97,97,97}, {97,0,65}, {130,130,130}, {195,195,195},
};

void enter_cave(SystemsState& state, int cave_index);
void exit_cave(SystemsState& state);

// Flat floor at y=168 across the cave width (L1/L5/L7; the L3
// table-driven layout lands with level 3).
void setup_cave_collision(SystemsState& state);

// Right-edge clamp + left-edge exit.  Call once per frame while in a cave.
void check_cave_exit(SystemsState& state);

// Descent ticks 2 and 3 (sprite 45, then the deferred cave entry); the arm
// frame's sprite 44 is drawn in collision_dispatch.  True while it owns the
// player update (the caller skips player physics).
bool tick_cave_descent(SystemsState& state);

}  // namespace olduvai::systems
