// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Seam-topology table (OL-B4) — the single encoding of non-contiguous
// screen seams, cross-checked against the facts the three former copies
// carried (widescreen suppressions, transition-kind classification, and
// the transitions.cpp warp authority's screen constants).

#include "doctest/doctest.h"
#include "systems/screen_topology.hpp"

using olduvai::systems::SeamKind;
using olduvai::systems::seam_contiguous;
using olduvai::systems::seam_kind;

TEST_CASE("topology: L3 trunk pocket + descent seams") {
    // S9 right-edge cave-warp -> S10; 10|11 vertical pocket halves;
    // S11 top -> S12 exit warp; 17->18 level-end descent.
    CHECK(seam_kind(3, 9, 10) == SeamKind::Warp);
    CHECK(seam_kind(3, 10, 11) == SeamKind::Warp);
    CHECK(seam_kind(3, 11, 12) == SeamKind::Warp);
    CHECK(seam_kind(3, 17, 18) == SeamKind::TrunkDescent);
    CHECK(seam_contiguous(3, 8, 9));
    CHECK(seam_contiguous(3, 12, 13));
    CHECK(seam_contiguous(3, 16, 17));
}

TEST_CASE("topology: L7 cave hall + fake cave seams") {
    // S9 cave-descent warp -> S10; hall interior 10|11|12 is walkable;
    // S12 -> S13 instant teleport (capstone 25b2:07df).
    CHECK(seam_kind(7, 9, 10) == SeamKind::Warp);
    CHECK(seam_contiguous(7, 10, 11));
    CHECK(seam_contiguous(7, 11, 12));
    CHECK(seam_kind(7, 12, 13) == SeamKind::FakeCaveInstant);
    CHECK(seam_kind(7, 13, 12) == SeamKind::FakeCaveInstant);  // symmetric
    CHECK(seam_contiguous(7, 13, 14));
}

TEST_CASE("topology: L1/L5 fully contiguous; non-adjacent pairs benign") {
    for (int s = 0; s < 19; ++s) {
        CHECK(seam_contiguous(1, s, s + 1));
        CHECK(seam_contiguous(5, s, s + 1));
    }
    CHECK(seam_contiguous(3, 9, 11));   // |a-b| != 1 -> Contiguous by contract
}
