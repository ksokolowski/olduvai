// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Screen-seam topology: which adjacent-screen seams are not contiguous
// horizontal walks.  One table for the widescreen peek suppression and the
// transition-kind classification; systems/transitions.cpp (capstone-cited)
// stays the authority for the warps, and a unit test cross-checks the two.
// EXE evidence per seam inline; anything unlisted is a contiguous walk.

#pragma once

namespace olduvai::systems {

enum class SeamKind {
    Contiguous,        // normal horizontal walk (pan transition)
    Warp,              // teleport/cave-warp — screens not spatially adjacent
    TrunkDescent,      // L3 17→18: the descent animation IS the transition
    FakeCaveInstant,   // L7 12↔13: EXE instant warp (capstone 25b2:07df
                       // jumps over the wipe); enhanced renders a fade
};

// Seam between surface screens `a` and `b` (either order).  Only |a-b| == 1 is
// meaningful; anything else returns Contiguous.
inline SeamKind seam_kind(int internal_level, int a, int b) {
    const int lo = a < b ? a : b;
    const int hi = a < b ? b : a;
    if (hi - lo != 1) return SeamKind::Contiguous;
    if (internal_level == 3) {
        // Dark Woods trunk pocket: the S9 right-edge cave warp to S10; 10|11 is
        // a vertical climb inside the trunk; the S11 top exits by warp to S12.
        if (lo == 9 || lo == 10 || lo == 11) return SeamKind::Warp;
        // Level-end giant-trunk descent (FUN_2276_03d9): vertical, down.
        if (lo == 17) return SeamKind::TrunkDescent;
    }
    if (internal_level == 7) {
        // Volcanic cave hall: S9 right edge clamps + cave-DESCENT warps to
        // S10 (10,131) — check_l7_transition.
        if (lo == 9) return SeamKind::Warp;
        // S12's right edge teleports to S13 (48,130): instant in the EXE
        // (25b2:07df); classic pans, enhanced fades.
        if (lo == 12) return SeamKind::FakeCaveInstant;
    }
    return SeamKind::Contiguous;
}

// A seam the widescreen peek may look across (and the pan may slide across
// as a spatial continuation).
inline bool seam_contiguous(int internal_level, int a, int b) {
    return seam_kind(internal_level, a, b) == SeamKind::Contiguous;
}

}  // namespace olduvai::systems
