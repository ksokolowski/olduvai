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

// Post-frame steps 6b-8a of `docs/FRAME_LOOP.md`, in the reference loop's
// order: death halo, L5 glider entry + screen-12 detach, clamp and
// death-by-fall, cave/secret exits, surface transitions, cave-warp animation.
// Runs immediately after run_frame; the caller keeps 8b (level-complete),
// 8c (secret bubble scatter) and 9 (screen change), which need the shell.
//
// Everything here reads and writes only SystemsState, which is what let it
// leave the shell (BACKLOG 3.6/3.7): a probe of the block reported exactly one
// free name.  ORDER IS THE CONTRACT — see docs/FRAME_LOOP.md before changing
// anything in it, including the order of the two glider calls.
void run_post_frame_steps(SystemsState& state);

}  // namespace olduvai::systems
