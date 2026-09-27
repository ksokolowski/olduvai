// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Boss arena presentation: the shared player/HUD render and the per-boss fight
// runners.  The arena background is the ring picture with its baked energy
// bar (columns 272..316), drained column by column as the boss loses health.

#pragma once

#include "presentation/game_app.hpp"          // GameOptions
#include "presentation/level/level_save.hpp"  // CarriedState
#include "presentation/pipeline.hpp"

namespace olduvai::presentation {

struct BossRunResult {
    bool survived = false;
    bool quit = false;
    bool restart = false;        // Pause -> Restart Fight (redo this level)
    bool quit_program = false;   // Pause -> Exit Game
};

// A boss fight (display levels 2/4/6 → internal 2/4/6), entry to exit.
// `carry`: the lives and score, in and out.  opts.frames > 0 exits after N
// frames; opts.screenshot saves frame opts.screenshot_frame.  opts.replay /
// opts.trace are the cross-engine harness files; opts.record_inputs records
// live inputs in the replay schema, at the frame the boss reader resolves.
BossRunResult run_boss_level(GameOptions& opts, int internal_level,
                             CarriedState& carry, Pipeline& pipe);

}  // namespace olduvai::presentation
