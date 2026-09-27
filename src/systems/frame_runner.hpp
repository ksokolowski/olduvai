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

// Step 1: the frame's inputs into the state.  The shell also calls it before
// run_frame, because flight physics steers from the live keys.
void apply_inputs(SystemsState& state, const FrameInputs& inputs);

void run_frame(SystemsState& state, const FrameInputs& inputs);

// The shell's tick, in docs/FRAME_LOOP.md's order.  The shell gathers the
// inputs between wrap_frame_counter and run_tick (they read the player after
// the wrap), and ticks its presentation-only state between run_tick and
// end_tick.

// The frame-counter wrap drives the timer and the food-out death: reset when
// the pre-increment value exceeded 0x3C, a 62-value cycle.  --god refills the
// timer instead.  // DS:0x985a
void wrap_frame_counter(SystemsState& state, bool god);

// Birds despawn and respawn `margin` px further out than the EXE's x < -50 /
// 355 (margin 0).
void set_bird_bounds(SystemsState& state, int margin);

// The inputs (flight physics steers from them before run_frame), a deferred
// cave-sign teleport (between the pre-frame snapshot and the classifier, so
// the classifier sees the cave->surface edge), the cave-entrance descent,
// then run_frame.  `paused` (the cheat picker) freezes the world: no
// teleport, no run_frame; the descent still ticks.
void run_tick(SystemsState& state, const FrameInputs& inputs, bool paused);

// --god's refill, then the post-frame steps 6b-8a.
void end_tick(SystemsState& state, bool god);

// Draw-timed counters: they gate draws, so the shell ticks each once per tick
// at a fixed point around the render (never per smooth sub-frame).
// Before the HUD draw: the teleport cloud countdown, departure first.
void tick_teleport_fx(SystemsState& state);
// After the HUD draw: GET READY, draw-then-decrement on even frames inside
// the window [2,17].  // FUN_27f7_1277, DS:0x97e0
void tick_get_ready(SystemsState& state);
// After the tick's last present, so every present path saw one value.
void tick_cave_emerge(SystemsState& state);

// --god: energy, lives and food topped and game over masked (as the
// reference does).  Death, ghost and respawn still run: clearing a fall death
// would strand the player below the screen (clamp_player_position never
// clamps Y).
void god_refill(SystemsState& state);

// Post-frame steps 6b-8a of `docs/FRAME_LOOP.md`, in the reference loop's
// order: death halo, L5 glider entry + screen-12 detach, clamp and
// death-by-fall, cave/secret exits, surface transitions, cave-warp animation.
// Runs immediately after run_frame; the caller keeps 8b (level-complete),
// 8c (secret bubble scatter) and 9 (screen change), which need the shell.
//
// Everything here reads and writes only SystemsState, which is what let it
// leave the shell after a probe of the block reported exactly one
// free name.  ORDER IS THE CONTRACT — see docs/FRAME_LOOP.md before changing
// anything in it, including the order of the two glider calls.
void run_post_frame_steps(SystemsState& state);

}  // namespace olduvai::systems
