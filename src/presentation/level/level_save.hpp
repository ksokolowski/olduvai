// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Full-state save capture / restore for the surface levels.
//
// capture_save/apply_save snapshot and restore the whole `Loaded` gameplay
// state (a checkpoint, and the in-place display reinit); CarriedState is what
// run_game threads between levels.  Regression-guarded by
// tests/reinit_smoke.sh (save -> reinit -> restore round-trip).

#pragma once

#include <string>

#include "presentation/level/level_state.hpp"  // Loaded
#include "presentation/level/save_state.hpp"   // SaveState
#include "presentation/menu/settings_apply.hpp"  // DisplaySettings

namespace olduvai::presentation {

struct CarriedState {
    int lives = 3;
    long score = 0;
};


SaveState capture_save(const Loaded& g, int display_level);
void apply_save(const SaveState& sv, Loaded& g);

}  // namespace olduvai::presentation
