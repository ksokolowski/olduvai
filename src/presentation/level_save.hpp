// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Full-state save capture / restore for the surface levels.
//
// Moved verbatim from game_app.cpp (CC2b, 2026-07-09). capture_save/apply_save
// snapshot and restore the whole `Loaded` gameplay state; CarriedState +
// PendingReinit are the small transient payloads run_platform_level/run_game
// thread through a settings-driven re-init. Regression-guarded by
// tests/reinit_smoke.sh (save -> reinit -> restore round-trip).

#pragma once

#include <string>

#include "presentation/level_state.hpp"  // Loaded
#include "presentation/save_state.hpp"   // SaveState

namespace olduvai::presentation {

struct CarriedState {
    int lives = 3;
    long score = 0;
};

// Transient payload for a settings-driven display/audio re-init: the target
// settings plus a full-state snapshot to restore after run_game rebuilds the
// window and/or audio.  Never serialized — lives only across the re-entry.
struct PendingReinit {
    bool enhanced = false;
    int render_scale = 2;
    std::string hd_profile = "native";
    std::string music_device = "auto";
    std::string sfx_backend = "auto";
    SaveState state;
};

SaveState capture_save(const Loaded& g, int display_level);
void apply_save(const SaveState& sv, Loaded& g);

}  // namespace olduvai::presentation
