// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// RisingBalloons: the Enhanced fly-away shared by the L1 landing and the boss
// fly-in (owner idea, 2026-09-17).  The drawn result is gated by
// tests/balloon_flyaway.sh; this pins the release rule, which is what a caller
// gets wrong — Classic must never arm it, a death must not (the game sends its
// own halo up), and the bunch belongs to the screen it was let go on.

#include "doctest/doctest.h"

#include "presentation/render/rising_balloons.hpp"
#include "systems/player.hpp"

using olduvai::presentation::RisingBalloons;

TEST_CASE("the bunch is released when the ride ends, and rises") {
    RisingBalloons b;
    CHECK(!b.active());
    b.step(true, true, true, 100, 70, 12);    // holding
    CHECK(!b.active());
    b.step(true, true, true, 105, 70, 12);
    CHECK(!b.active());
    b.step(true, false, true, 110, 70, 12);   // let go
    CHECK(b.active());
    for (int i = 0; i < 4; ++i) b.step(true, false, true, 0, 0, 12);
    CHECK(b.active());
    // 8 px per tick (systems::kBalloonRisePerTick): from y=70 it clears the
    // top well inside 20 ticks.
    for (int i = 0; i < 20; ++i) b.step(true, false, true, 0, 0, 12);
    CHECK(!b.active());
}

TEST_CASE("Classic never arms it") {
    RisingBalloons b;
    b.step(false, true, true, 100, 70, 12);
    b.step(false, false, true, 100, 70, 12);
    CHECK(!b.active());
}

TEST_CASE("a death does not release the bunch") {
    RisingBalloons b;
    b.step(true, true, true, 100, 70, 12);
    b.step(true, false, false, 100, 70, 12);   // died in flight
    CHECK(!b.active());
}

TEST_CASE("a screen change ends the effect") {
    RisingBalloons b;
    b.step(true, true, true, 100, 70, 12);
    b.step(true, false, true, 100, 70, 12);
    REQUIRE(b.active());
    b.step(true, false, true, 100, 70, 13);    // walked on
    CHECK(!b.active());
}
