// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// transition_shift — where the outgoing and incoming screens sit during a
// transition (BACKLOG §3.12; src/presentation/sequence/transition_geometry.hpp).
//
// The two players wrote this arithmetic out twice.  What the pixels actually
// require is pinned here as a PROPERTY rather than a table of magic numbers:
// the two screens are exactly one buffer apart for the whole pan, so the
// seam between them never overlaps (a doubled column) and never gaps (a
// black seam) — the defect class the wide transitions kept producing
// ("tearing", the over-scroll by 2M that §panorama fixed).
#include "doctest/doctest.h"
#include "presentation/sequence/transition_geometry.hpp"

// <initializer_list> for the `for (x : {a, b, c})` loops below: libc++ pulls
// it in transitively, libstdc++ does not, so this file built on the dev Mac
// and broke every GCC job on CI.
#include <initializer_list>
// <cmath> because THIS file calls std::lround (the arc case below).  It
// compiles without it — transition_geometry.hpp includes it — and that is
// precisely the dependency this file's own header warns about.
#include <cmath>

using olduvai::presentation::transition_shift;
using olduvai::presentation::TransitionShift;

namespace {
constexpr int W = 320;
constexpr int H = 200;
}  // namespace

TEST_CASE("transition_shift: the two screens stay exactly one buffer apart") {
    for (const char dir : {'R', 'L', 'D', 'U'}) {
        for (const double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            const TransitionShift s = transition_shift(1, dir, t, W, H);
            if (dir == 'R' || dir == 'L') {
                CHECK(s.ody == 0);
                CHECK(s.ndy == 0);
                CHECK(s.ndx - s.odx == (dir == 'R' ? W : -W));
            } else {
                CHECK(s.odx == 0);
                CHECK(s.ndx == 0);
                CHECK(s.ndy - s.ody == (dir == 'D' ? H : -H));
            }
        }
    }
}

TEST_CASE("transition_shift: a pan starts on the old screen and ends on the new") {
    // t=0: the old screen is exactly in frame, the new one is off it.
    const TransitionShift a = transition_shift(1, 'R', 0.0, W, H);
    CHECK(a.odx == 0);
    CHECK(a.ndx == W);
    // t=1: the new screen has taken the frame.
    const TransitionShift b = transition_shift(1, 'R', 1.0, W, H);
    CHECK(b.odx == -W);
    CHECK(b.ndx == 0);
    // Leftwards is the mirror of that.
    CHECK(transition_shift(1, 'L', 1.0, W, H).odx == W);
    CHECK(transition_shift(1, 'L', 1.0, W, H).ndx == 0);
}

TEST_CASE("transition_shift: the secret slides ignore dir and move vertically") {
    // kind 3 = entry (old slides UP, new arrives from the bottom),
    // kind 4 = exit (old slides DOWN, new arrives from the top).  Both carry
    // a direction char from the caller that must NOT change them.
    for (const char dir : {'R', 'L', 'D', 'U'}) {
        const TransitionShift in = transition_shift(3, dir, 0.5, W, H);
        CHECK(in.odx == 0);
        CHECK(in.ndx == 0);
        CHECK(in.ody == -H / 2);
        CHECK(in.ndy == H / 2);

        const TransitionShift out = transition_shift(4, dir, 0.5, W, H);
        CHECK(out.ody == H / 2);
        CHECK(out.ndy == -H / 2);
    }
    // Ends: fully swapped, in both directions.
    CHECK(transition_shift(3, 'U', 1.0, W, H).ndy == 0);
    CHECK(transition_shift(3, 'U', 1.0, W, H).ody == -H);
    CHECK(transition_shift(4, 'U', 1.0, W, H).ndy == 0);
    CHECK(transition_shift(4, 'U', 1.0, W, H).ody == H);
}

TEST_CASE("transition_shift: units are the caller's, and it is pure arithmetic") {
    // play_transition passes its BUFFER size (HD when the buffer is HD);
    // play_transition_wide passes NATIVE size because its blitter scales.
    // Same function, so scaling the inputs scales the outputs exactly.
    const TransitionShift native = transition_shift(1, 'R', 0.5, 320, 200);
    const TransitionShift hd = transition_shift(1, 'R', 0.5, 320 * 3, 200 * 3);
    CHECK(hd.odx == native.odx * 3);
    CHECK(hd.ndx == native.ndx * 3);

    // A wide canvas is wider than 320 and pans by its own width.
    const TransitionShift wide = transition_shift(1, 'R', 1.0, 320 + 2 * 73, 200);
    CHECK(wide.odx == -(320 + 2 * 73));
}

// ── arc_overlay_pos ─────────────────────────────────────────────────────────
//
// The kind-4 exit arc, written out twice by the same two players.  What the
// pixels require is again a PROPERTY, not a table: the two phases must MEET
// (no pop when the pan hands over to the arc) and the arc must END exactly on
// the player's resume position (no end snap).  Both were previously asserted
// only by eye, plus the draw-log harness a person has to read.
TEST_CASE("arc_overlay_pos: the phases meet and the landing does not snap") {
    using olduvai::presentation::arc_overlay_pos;
    using olduvai::presentation::ArcOverlay;

    constexpr int kPan = 30, kArc = 26;       // play_transition's kind-4 pair
    constexpr int kSx = 40, kEx = 210, kEy = 120, kBake = 185;
    constexpr double kPeak = 30.0;

    // The handover: the last pan sample and the first arc sample are the same
    // point.  A mismatch here is the "start-of-slide pop".
    const ArcOverlay pan_end =
        arc_overlay_pos(kPan, 1.0, kPan, kArc, kSx, kEx, kEy, kBake, kPeak);
    const ArcOverlay arc_start =
        arc_overlay_pos(kPan + 1e-9, 1.0, kPan, kArc, kSx, kEx, kEy, kBake,
                        kPeak);
    CHECK(pan_end.x == kSx);
    CHECK(pan_end.y == kBake);
    CHECK(arc_start.x == pan_end.x);
    CHECK(arc_start.y == pan_end.y);

    // The landing: at the final frame the overlay IS the resume position.
    const ArcOverlay landed =
        arc_overlay_pos(kPan + kArc, 1.0, kPan, kArc, kSx, kEx, kEy, kBake,
                        kPeak);
    CHECK(landed.x == kEx);
    CHECK(landed.y == kEy);

    // The arc arcs: halfway through, the sprite is ABOVE the straight line
    // between bake point and resume by the peak (smaller y = higher).
    const ArcOverlay mid =
        arc_overlay_pos(kPan + kArc / 2.0, 1.0, kPan, kArc, kSx, kEx, kEy,
                        kBake, kPeak);
    const int straight = static_cast<int>(
        std::lround(kBake + (kEy - kBake) * 0.5));
    CHECK(mid.y < straight);
    CHECK(straight - mid.y == static_cast<int>(kPeak));

    // Phase 1 rides IN with the surface: at t = 0 the sprite sits a full
    // screen below the bake point, and it climbs to the bake point at t = 1.
    const ArcOverlay ride_start =
        arc_overlay_pos(0.0, 0.0, kPan, kArc, kSx, kEx, kEy, kBake, kPeak);
    CHECK(ride_start.y == kBake - 200);
}
