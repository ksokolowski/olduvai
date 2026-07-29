// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The smooth-motion snap/lerp family — BACKLOG 3.3b step 2's safety net.
//
// These four rules were open-coded at ~13 call sites across two drivers before
// the unification, in four slightly different spellings.  Pinning them here is
// what makes collapsing those sites a mechanical change rather than a hopeful
// one, because the lerp bodies themselves are otherwise nearly ungated:
// `smooth` needs `smooth_motion && frames <= 0 && shot.empty()` and every
// gate but `cave_lerp` falsifies a term (see BACKLOG 3.3b).
//
// SDL-free and asset-free on purpose — this runs in `unit`, so CI executes it.
#include "doctest/doctest.h"

#include "presentation/render/lerp_snapshot.hpp"

using olduvai::presentation::kSnapPx;
using olduvai::presentation::snap_jumped;
using olduvai::presentation::snap_lerp_f;
using olduvai::presentation::snap_lerp_i;
using olduvai::presentation::snap_lerp_pair;

TEST_CASE("snap threshold is exclusive — exactly kSnapPx still interpolates") {
    // The guard is `> kSnapPx`, not `>=`.  A caller that "tidied" this to >=
    // would stop interpolating legitimate fast movement.
    CHECK_FALSE(snap_jumped(0, kSnapPx));
    CHECK(snap_jumped(0, kSnapPx + 1));
    CHECK(snap_jumped(0, -(kSnapPx + 1)));   // symmetric in sign
}

TEST_CASE("alpha endpoints are exact") {
    // alpha == 1 must land exactly on cur, or the last sub-frame of every tick
    // sits one rounding away from the logic position it is supposed to match.
    CHECK(snap_lerp_i(10, 14, 1.0f) == 14);
    CHECK(snap_lerp_f(10, 14, 1.0f) == doctest::Approx(14.0f));
    CHECK(snap_lerp_i(10, 14, 0.0f) == 10);
    CHECK(snap_lerp_f(10, 14, 0.0f) == doctest::Approx(10.0f));
}

TEST_CASE("a teleport snaps to cur at every alpha") {
    const int far = kSnapPx + 5;
    for (float a : {0.0f, 0.33f, 0.5f, 1.0f}) {
        CHECK(snap_lerp_i(0, far, a) == far);
        CHECK(snap_lerp_f(0, far, a) == doctest::Approx(static_cast<float>(far)));
    }
}

TEST_CASE("force snaps regardless of distance — the cave-entry rule") {
    // The L3 screen-4 cave entry moves the player only (9,11) px: UNDER the
    // threshold, so distance alone says "interpolate" and the sprite sweeps
    // across the screen.  Only the caller knows a screen change happened.
    CHECK(snap_jumped(0, 9, /*force=*/true));
    CHECK(snap_lerp_i(100, 109, 0.5f, /*force=*/true) == 109);
    CHECK(snap_lerp_f(100, 109, 0.5f, /*force=*/true) == doctest::Approx(109.0f));
    // ...and without force the same move DOES interpolate.
    CHECK(snap_lerp_i(100, 109, 0.5f) != 109);
}

TEST_CASE("the pair guards on EITHER axis and snaps BOTH") {
    // This is the rule that makes the pair form irreducible to two scalar
    // calls: y teleports, so x must snap too — otherwise a diagonal warp
    // half-interpolates and the sprite streaks.
    const auto r = snap_lerp_pair(/*prevx=*/0, /*prevy=*/0,
                                  /*curx=*/4, /*cury=*/kSnapPx + 1, 0.5f);
    CHECK(r.x == 4);                       // snapped, NOT 0 + lround(4*0.5)=2
    CHECK(r.y == kSnapPx + 1);
    CHECK(r.fx == doctest::Approx(4.0f));
    CHECK(r.fy == doctest::Approx(static_cast<float>(kSnapPx + 1)));

    // Sanity: the same x alone, scalar, DOES interpolate — proving the snap
    // above came from the y axis and not from x being out of range.
    CHECK(snap_lerp_i(0, 4, 0.5f) == 2);
}

TEST_CASE("the pair's int and float shadows always take the same branch") {
    // One branch, one result: the two shadows used to be two `if` statements
    // with the same condition written out twice, which is a decision that can
    // diverge.  Sweep the neighbourhood of the threshold on both axes.
    //
    // Assert the RULE directly rather than inferring "did it snap?" from the
    // output — the first cut of this test did infer it, via `r.x == dx`, and
    // failed: with dx=1, alpha=0.5, `lround(0.5) == 1` lands on cur by
    // arithmetic coincidence while the float lands on 0.5.  Landing on cur is
    // not the same as snapping to it, and a test that cannot tell them apart
    // is measuring rounding, not branching.
    for (int dx = -20; dx <= 20; ++dx) {
        for (int dy : {0, kSnapPx, kSnapPx + 1}) {
            const auto r = snap_lerp_pair(0, 0, dx, dy, 0.5f);
            const bool jumped = snap_jumped(0, dx) || snap_jumped(0, dy);
            if (jumped) {
                CHECK(r.x == dx);
                CHECK(r.y == dy);
                CHECK(r.fx == doctest::Approx(static_cast<float>(dx)));
                CHECK(r.fy == doctest::Approx(static_cast<float>(dy)));
            } else {
                CHECK(r.x == snap_lerp_i(0, dx, 0.5f));
                CHECK(r.y == snap_lerp_i(0, dy, 0.5f));
                CHECK(r.fx == doctest::Approx(snap_lerp_f(0, dx, 0.5f)));
                CHECK(r.fy == doctest::Approx(snap_lerp_f(0, dy, 0.5f)));
            }
        }
    }
}

TEST_CASE("the pair matches the scalars when neither axis teleports") {
    // Equivalence with the open-coded form the call sites used: inside the
    // guard, each axis is exactly the scalar lerp.
    const auto r = snap_lerp_pair(10, 20, 14, 26, 0.25f);
    CHECK(r.x == snap_lerp_i(10, 14, 0.25f));
    CHECK(r.y == snap_lerp_i(20, 26, 0.25f));
    CHECK(r.fx == doctest::Approx(snap_lerp_f(10, 14, 0.25f)));
    CHECK(r.fy == doctest::Approx(snap_lerp_f(20, 26, 0.25f)));
}
