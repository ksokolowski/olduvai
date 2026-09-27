// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Fluid-bubble physics: persistent rising bubbles for the enhanced secret room,
// a visual replacing the EXE's per-frame LCG scatter (the scatter still rolls
// the global LCG; this never touches it).  Uses its own core::RandLcg16 (the
// reference's cosmetic LCG, mult 0x015A4E35), so the stream matches the
// reference from the shared seed and the game LCG stays identical across
// modes.
// Per tick:
//   prev_x/prev_y = x/y            (smooth-motion snapshot)
//   y -= vy                        (rise)
//   wobble_t += wobble_freq
//   x = center_x + sin(wobble_t) * wobble_amp
//   y < kDespawnY -> respawn at the bottom with fresh parameters

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

    // One logic tick: snapshot prev, advance y / wobble / x, respawn despawned
    // bubbles in place.
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
