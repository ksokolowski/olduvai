// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Cave system — entry/exit, collision, boundaries.  // Cave dispatcher
// FUN_2759_0645; per-level inits FUN_2759_027a/04ee/0428/033a
//
// Entry: 2-frame descent animation (sprites 44 lit → 45 dim; sprite 46 is
// unreachable in the EXE flow — see cave_enter_exit_presentation_model.md)
// driven by cave_entrance_mask
// (bits[1:0] = frame, bits[7:2] = cave index), then the teleport into
// current_screen = 100 + cave_index.  Exit: walk left past x=6
// (L1/L5/L7), or the cave-sign teleport (collision dispatch).  The
// counter==1000 entrance is the L7 screen-transition marker.

#pragma once

#include "systems/player.hpp"

namespace olduvai::systems {

constexpr int kCaveSpawnY = 139;
constexpr int kSprCaveDescent1 = 44;

// Cave-EMERGE reveal length (ticks armed by exit_cave).  Enhanced v2
// pacing (owner directive 2026-07-05, matches the Enhanced #20 teleport
// idiom): 3 dim stages x 3-tick holds = 9 ticks (1/3 -> 2/3 -> full thirds
// of PLAYER_TURN), with game time frozen for the player (frame_runner).
// Classic: 2 lit ticks, draw-only, NO freeze — classic gameplay timing
// stays EXE-identical.
constexpr int kCaveEmergeTicksEnhanced = 9;
constexpr int kCaveEmergeTicksClassic = 2;
constexpr int kCaveEmergeStageHold = 3;   // ticks per dim stage (enhanced)

// Cave palettes (16 RGB entries per level) — hand-authored approximations
// of the cave tint (note the uniform quantized steps: every component is a
// multiple of one 6-bit increment).  The original applies its own palette
// at cave entry (FUN_2759_00b7); these VALUES are the project's and appear
// nowhere in the executable (byte-searched 2026-07-19, both 6-bit
// encodings — not a copied data block).
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

// Descent animation ticks 2 and 3 (sprite 45, then the deferred cave enter;
// the ARM frame's sprite-44 draw lives in collision_dispatch, matching the
// EXE's same-frame arm+draw).  Returns true while it owns the player update
// this frame (caller skips the player physics step).
bool tick_cave_descent(SystemsState& state);

}  // namespace olduvai::systems
