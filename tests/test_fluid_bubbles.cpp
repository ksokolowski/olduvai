// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// TDD: Fluid-bubble physics — written BEFORE the implementation.
//
// Covers: spawn ranges, rise monotonicity between respawns, respawn-
// after-despawn, prev/cur tracking, and the 16-px snap-guard
// classification.  Matches the render-parity bundle design spec (§F7)
// and the Python reference implementation's fluid-bubble system.

#include "doctest/doctest.h"

#include "systems/fluid_bubbles.hpp"

using namespace olduvai::systems;

// ── Spawn ranges ────────────────────────────────────────────────────────────

TEST_CASE("fluid bubbles: init populates exactly kBubbleCount bubbles") {
    FluidBubbleSystem fbs;
    fbs.init();
    CHECK(fbs.bubbles().size() == static_cast<std::size_t>(kBubbleCount));
}

TEST_CASE("fluid bubbles: initial y is in [0, kPlayfieldH) (whole-field scatter)") {
    FluidBubbleSystem fbs;
    fbs.init();
    for (const auto& b : fbs.bubbles()) {
        CHECK(b.y >= 0.0f);
        CHECK(b.y < kPlayfieldH);
    }
}

TEST_CASE("fluid bubbles: initial x is in [0, kScreenW)") {
    FluidBubbleSystem fbs;
    fbs.init();
    for (const auto& b : fbs.bubbles()) {
        CHECK(b.x >= 0.0f);
        CHECK(b.x < kScreenW + kWobbleAmpMax); // center_x in [0,320); x = center + sin*amp
    }
}

TEST_CASE("fluid bubbles: vy is in [kVyMin, kVyMax]") {
    FluidBubbleSystem fbs;
    fbs.init();
    for (const auto& b : fbs.bubbles()) {
        CHECK(b.vy >= kVyMin);
        CHECK(b.vy <= kVyMax);
    }
}

TEST_CASE("fluid bubbles: wobble_freq is in [kWobbleFreqMin, kWobbleFreqMax]") {
    FluidBubbleSystem fbs;
    fbs.init();
    for (const auto& b : fbs.bubbles()) {
        CHECK(b.wobble_freq >= kWobbleFreqMin);
        CHECK(b.wobble_freq <= kWobbleFreqMax);
    }
}

TEST_CASE("fluid bubbles: wobble_amp is in [kWobbleAmpMin, kWobbleAmpMax]") {
    FluidBubbleSystem fbs;
    fbs.init();
    for (const auto& b : fbs.bubbles()) {
        CHECK(b.wobble_amp >= kWobbleAmpMin);
        CHECK(b.wobble_amp <= kWobbleAmpMax);
    }
}

TEST_CASE("fluid bubbles: sprite_idx is 17 or 18") {
    FluidBubbleSystem fbs;
    fbs.init();
    for (const auto& b : fbs.bubbles()) {
        CHECK((b.sprite_idx == 17 || b.sprite_idx == 18));
    }
}

// ── Rise monotonicity ────────────────────────────────────────────────────────

TEST_CASE("fluid bubbles: y decreases (rises) each tick between respawns") {
    FluidBubbleSystem fbs;
    fbs.init();
    // Tick 30 times; every bubble that doesn't respawn must have lower y.
    // To avoid respawns confusing this, seed the system so all bubbles start
    // well below despawn (initial scatter y in [0, 200)).  A fresh init
    // guarantees y >= 0, so y < kDespawnY = -8 won't trigger for at least
    // 1 tick per bubble (vy <= 3.0, so 0 - 3 = -3 > -8).
    // We just check that after one tick, y < prev_y for each bubble
    // that didn't just respawn (detect respawn by a large jump).
    const auto before = fbs.bubbles();
    fbs.tick();
    const auto& after = fbs.bubbles();
    for (std::size_t i = 0; i < after.size(); ++i) {
        const float dy = after[i].y - before[i].y;
        // If the bubble respawned (large downward jump from ~-8 to ~175+),
        // skip it.  Otherwise y must have decreased.
        const bool respawned = (dy > kSnapThreshold);
        if (!respawned) {
            CHECK(after[i].y < before[i].y);
        }
    }
}

// ── Respawn after despawn ────────────────────────────────────────────────────

TEST_CASE("fluid bubbles: bubble respawns below floor after crossing kDespawnY") {
    FluidBubbleSystem fbs;
    fbs.init();
    // Force the first bubble above despawn threshold.
    auto& first = fbs.bubbles_mutable()[0];
    first.y = kDespawnY + 0.5f;   // just above (will despawn after one tick)
    first.vy = 2.0f;
    fbs.tick();
    const auto& b = fbs.bubbles()[0];
    // After respawn: y must be in [kSpawnYMin, kSpawnYMax]
    CHECK(b.y >= kSpawnYMin);
    CHECK(b.y <= kSpawnYMax);
    // And vy is still in valid range.
    CHECK(b.vy >= kVyMin);
    CHECK(b.vy <= kVyMax);
}

// ── prev/cur tracking ────────────────────────────────────────────────────────

TEST_CASE("fluid bubbles: prev_y equals y before first tick after init") {
    FluidBubbleSystem fbs;
    fbs.init();
    for (const auto& b : fbs.bubbles()) {
        // After init, prev_ = cur_ (no interpolation artefact on first frame).
        CHECK(b.prev_y == doctest::Approx(b.y));
        CHECK(b.prev_x == doctest::Approx(b.x));
    }
}

TEST_CASE("fluid bubbles: prev_y captures y-before-tick after each tick") {
    FluidBubbleSystem fbs;
    fbs.init();
    // Force a stable bubble that won't respawn in 5 ticks (y = 100, vy = 1).
    auto& b0 = fbs.bubbles_mutable()[0];
    b0.y = 100.0f;
    b0.vy = 1.0f;
    b0.wobble_amp = 0.0f;  // keep x stable
    const float y_before = b0.y;
    fbs.tick();
    // After tick: prev_y == y_before, cur y == y_before - 1.0
    CHECK(fbs.bubbles()[0].prev_y == doctest::Approx(y_before));
    CHECK(fbs.bubbles()[0].y == doctest::Approx(y_before - 1.0f));
}

// ── Snap-guard classification ─────────────────────────────────────────────────

TEST_CASE("fluid bubbles: respawn sets prev_y == new y (no sweep artefact on respawn frame)") {
    // The Python reference sets prev_x/prev_y = new.x/new.y on respawn so
    // the sub-frame lerp sees delta=0 and stays at the new position rather
    // than sweeping from -8 to 175+ across three sub-frames.
    FluidBubbleSystem fbs;
    fbs.init();
    auto& b0 = fbs.bubbles_mutable()[0];
    b0.y = kDespawnY + 0.5f;  // just above; one tick of vy >= 1 puts it below kDespawnY
    b0.vy = 2.0f;
    fbs.tick();
    const auto& b = fbs.bubbles()[0];
    // After respawn: prev_y == y (no lerp sweep possible).
    CHECK(b.prev_y == doctest::Approx(b.y));
    // And the new position is in the spawn band.
    CHECK(b.y >= kSpawnYMin);
    CHECK(b.y <= kSpawnYMax);
}

TEST_CASE("fluid bubbles: normal rise step dy is within snap guard") {
    FluidBubbleSystem fbs;
    fbs.init();
    // Force a fast bubble: vy=kVyMax=3.0 < kSnapThreshold=16 — always within guard.
    auto& b0 = fbs.bubbles_mutable()[0];
    b0.y = 100.0f;
    b0.vy = kVyMax;
    b0.wobble_amp = 0.0f;
    const float prev_y_before = b0.y;
    fbs.tick();
    // prev_y was captured at y=100, cur y = 100 - 3 = 97; dy = 3 <= 16.
    CHECK(std::abs(fbs.bubbles()[0].y - fbs.bubbles()[0].prev_y) <= kSnapThreshold);
    // prev_y should be the y captured before the tick.
    CHECK(fbs.bubbles()[0].prev_y == doctest::Approx(prev_y_before));
}

// ── Respawn spawn-y bounds ───────────────────────────────────────────────────

TEST_CASE("fluid bubbles: many respawns all land in [kSpawnYMin, kSpawnYMax]") {
    FluidBubbleSystem fbs;
    fbs.init();
    // Drive all bubbles past the despawn line and verify each respawn landing.
    for (auto& b : fbs.bubbles_mutable()) {
        b.y = kDespawnY + 0.5f;  // one tick above despawn
    }
    fbs.tick();  // should trigger 60 respawns
    for (const auto& b : fbs.bubbles()) {
        CHECK(b.y >= kSpawnYMin);
        CHECK(b.y <= kSpawnYMax);
    }
}
