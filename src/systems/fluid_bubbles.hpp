// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Fluid-bubble physics — persistent rising bubbles for the secret room.
//
// Enhanced-mode replacement for the EXE's 627-draw-per-frame LCG scatter.
// The scatter STILL RUNS (refresh_secret_tiles in game_app.cpp keeps rolling
// the global LCG) — this system only provides the persistent state for an
// alternative VISUAL.  The LCG stream is never touched here.
//
// Design matches the reference implementation.
//
// PRNG: a dedicated core::RandLcg16 instance (the EXE LCG, seeded once) —
// NEVER the game global_rng().  Using the same LCG algorithm as Python's
// _cosmetic_rng (mult 0x015A4E35) makes the bubble stream byte-identical to
// the reference from the shared seed; std::minstd_rand was a different LCG so
// every bubble diverged.  The global game LCG must advance identically in
// classic and enhanced modes for replay/trace parity, hence a SEPARATE
// instance here, not global_rng().
//
// Physics per tick:
//   prev_x/prev_y = x/y (snapshot for smooth-motion lerp)
//   y -= vy                          (rise)
//   wobble_t += wobble_freq
//   x = center_x + sin(wobble_t) * wobble_amp
//   if y < kDespawnY → respawn at bottom with fresh random params
//
// Constants are public so tests can reference them.

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/rng.hpp"

namespace olduvai::systems {

// ── Public constants (also used by tests) ──────────────────────────────────
constexpr int   kBubbleCount    = 60;
constexpr float kScreenW        = 320.0f;
constexpr float kPlayfieldH     = 200.0f;
constexpr float kSpawnYMin      = 175.0f;  // below secret-screen floor (168)
constexpr float kSpawnYMax      = 220.0f;  // off-screen below
constexpr float kDespawnY       = -8.0f;   // off-screen above
constexpr float kVyMin          = 1.0f;
constexpr float kVyMax          = 3.0f;
constexpr float kWobbleFreqMin  = 0.05f;   // rad/tick
constexpr float kWobbleFreqMax  = 0.15f;
constexpr float kWobbleAmpMin   = 0.5f;    // px
constexpr float kWobbleAmpMax   = 2.0f;
constexpr float kSnapThreshold  = 16.0f;   // respawn jump >> this; normal dy << this

struct FluidBubble {
    float x = 0.0f, y = 0.0f;
    float vy = 1.0f;
    float wobble_t = 0.0f;
    float wobble_freq = 0.1f;
    float wobble_amp  = 1.0f;
    float center_x   = 0.0f;
    int   sprite_idx  = 17;       // 17 or 18 (ELEML1.MAT bubble sprites)
    float prev_x = 0.0f, prev_y = 0.0f;
};

class FluidBubbleSystem {
public:
    FluidBubbleSystem();

    // Populate 60 bubbles with steady-state y distribution (scattered across
    // [0, kPlayfieldH)) so the scene is full from frame 1.
    void init();

    // Advance physics one logic tick (18 Hz).
    // Snapshots prev_x/prev_y, then advances y, wobble, x.
    // Despawned bubbles are replaced in-place with fresh respawn params.
    void tick();

    const std::vector<FluidBubble>& bubbles() const { return bubbles_; }

    // Mutable accessor for tests that need to force specific states.
    std::vector<FluidBubble>& bubbles_mutable() { return bubbles_; }

private:
    core::RandLcg16 rng_;
    std::vector<FluidBubble> bubbles_;

    float randf(float lo, float hi);
    FluidBubble make_bubble(bool initial);
};

}  // namespace olduvai::systems
