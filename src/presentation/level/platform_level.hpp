// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// One platform level, entry to exit: the frame loop of docs/FRAME_LOOP.md
// (presentation/game_app.cpp).
#pragma once

#include <optional>

#include "presentation/game_app.hpp"          // GameOptions
#include "presentation/level/level_save.hpp"  // CarriedState
#include "presentation/level/save_state.hpp"  // SaveState
#include "presentation/pipeline.hpp"

namespace olduvai::presentation {

enum class LevelOutcome { kComplete, kGameOver, kQuit, kQuitProgram,
                          kRestartLevel, kLoadCheckpoint, kWarpLevel };

// `restore_in`: a checkpoint to enter at.  Pause writes `out_load` (Load
// Game) or `out_warp_display` (Cheats -> Warp!) with the matching outcome.
// A display or audio Apply rebuilds in place through `pipe.adopt`: the level
// stays.
LevelOutcome run_platform_level(GameOptions& opts, int display_level,
                                int internal, CarriedState& carry,
                                Pipeline& pipe,
                                const std::optional<SaveState>& restore_in,
                                std::optional<SaveState>& out_load,
                                int& out_warp_display);

}  // namespace olduvai::presentation
