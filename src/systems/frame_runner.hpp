// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Deterministic single-frame runner — no display, no audio, no I/O.
// Advances the state by exactly one game-logic iteration in the canonical
// order: inputs → popup decrement → entity update → fireball spawn/move →
// falling stone → player-entity collisions → player physics → popup move →
// frame counter.  The level setup (entities, collision bitmap) is the
// caller's responsibility.

#pragma once

#include "systems/player.hpp"

namespace olduvai::systems {

struct FrameInputs {
    bool left = false, right = false, up = false, down = false,
         attack = false, jump = false;   // jump is an alias of up
};

// Rolling-stone per-frame tick.  Returns true on a player hit.
bool update_falling_stone(SystemsState& state);

void run_frame(SystemsState& state, const FrameInputs& inputs);

}  // namespace olduvai::systems
