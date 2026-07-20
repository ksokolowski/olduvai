// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/fluid_bubbles.hpp"

#include <cmath>

namespace olduvai::systems {

namespace {
// M_PI is not part of standard C++ and is hidden in strict -std=c++17 mode
// (MinGW/MSVC); a local constant keeps the file self-contained.
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

FluidBubbleSystem::FluidBubbleSystem()
    : rng_(0x0B0B1E5u) {   // deterministic cosmetic seed (mirrors Python _cosmetic_rng)
}

float FluidBubbleSystem::randf(float lo, float hi) {
    // Uniform float in [lo, hi).  Uses the dedicated cosmetic PRNG so
    // the global game LCG is NEVER advanced by this system.
    const float unit = static_cast<float>(rng_.next() % 1000u) / 1000.0f;
    return lo + unit * (hi - lo);
}

FluidBubble FluidBubbleSystem::make_bubble(bool initial) {
    FluidBubble b;
    b.center_x    = randf(0.0f, kScreenW);
    b.y           = initial ? randf(0.0f, kPlayfieldH)
                            : randf(kSpawnYMin, kSpawnYMax);
    b.vy          = randf(kVyMin, kVyMax);
    b.wobble_t    = randf(0.0f, 2.0f * kPi);
    b.wobble_freq = randf(kWobbleFreqMin, kWobbleFreqMax);
    b.wobble_amp  = randf(kWobbleAmpMin, kWobbleAmpMax);
    // Sprite 17 or 18: use rng_ bit 0 of a fresh draw.
    b.sprite_idx  = 17 + static_cast<int>(rng_.next() % 2u);
    b.x           = b.center_x + std::sin(b.wobble_t) * b.wobble_amp;
    b.prev_x      = b.x;
    b.prev_y      = b.y;
    return b;
}

void FluidBubbleSystem::init() {
    bubbles_.clear();
    bubbles_.reserve(kBubbleCount);
    for (int i = 0; i < kBubbleCount; ++i) {
        bubbles_.push_back(make_bubble(/*initial=*/true));
    }
}

void FluidBubbleSystem::tick() {
    for (auto& b : bubbles_) {
        // Snapshot for smooth-motion lerp.
        b.prev_x = b.x;
        b.prev_y = b.y;

        // Physics advance.
        b.y -= b.vy;
        b.wobble_t += b.wobble_freq;
        b.x = b.center_x + std::sin(b.wobble_t) * b.wobble_amp;

        // Despawn → respawn at bottom with fresh parameters.
        if (b.y < kDespawnY) {
            b = make_bubble(/*initial=*/false);
        }
    }
}

}  // namespace olduvai::systems
