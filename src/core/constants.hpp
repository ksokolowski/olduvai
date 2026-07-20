// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Engine-wide gameplay constants.  Every game-behaviour value cites its
// evidence; values are reimplemented logic (authored constants), never
// copied data blocks.

#pragma once

#include <array>
#include <cstdint>

namespace olduvai::core {

// Native resolution + logic rate.
constexpr int kGameW = 320;
constexpr int kGameH = 200;
constexpr int kGameFps = 18;            // FUN_1847_16ad (frame wait)

// Level progression: internal level IDs in play order.  Display level N
// (the in-game "Level N") = index N-1 here.  Only slots 3 and 5 swap.
constexpr std::array<int, 7> kGameLevelOrder = {1, 2, 5, 4, 3, 6, 7};
                                        // FUN_2bd7_04be (7 lcalls in order)
constexpr int kLastScreen = 18;         // screen 19 (0x13) triggers level end
constexpr std::array<int, 3> kBossLevels = {2, 4, 6};

// Player initial state.
constexpr int kInitialEnergy = 10;      // energy pips, full
constexpr int kInitialLives = 3;
constexpr int kJumpYVel = 20;           // y_vel reset value (0x14)

// Sequencer waits (seconds; fire-key-skippable delays).
constexpr int kGameoverHoldSeconds = 8;       // FUN_2bd7_02e7 +0x034b
constexpr int kTitleIntermediateSeconds = 20; // FUN_2bd7_0362 waits
constexpr int kScoretallyPauseSeconds = 4;    // FUN_270a_01b4 +0x148/+0x20f

// Cave system.  // FUN_2759_0645 dispatcher
constexpr int kCaveFloorY = 168;
constexpr int kCavePlayfieldTop = 9;
constexpr int kCaveEnterX = 40;
// Cave interior widths are game data, not engine constants — read from the
// user's executable at startup (prepare::read_cave_size_table) into
// core::game_tables().cave_sizes.  Index ranges: L1 0-21, L3 22-25 (unused
// at runtime — the L3 exit edge is hardcoded), L5 26-38, L7 39-52.
// counter == 1000 on a cave entrance marks an L7 screen transition.
constexpr int kCaveTransitionMarker = 1000;

// Entity types whose record y is a foot baseline (the handler subtracts the
// sprite height before enqueueing): SECRET_FOOD (7), FOOD_CAVE (20).
constexpr std::array<int, 2> kBottomAlignedTypes = {7, 20};

// Title-intro image sequence.
constexpr std::array<const char*, 4> kIntroScreens = {
    "TITUS.PC1", "TITRE1.PC1", "TITRE2.PC1", "BULLE.PC1",
};

}  // namespace olduvai::core
