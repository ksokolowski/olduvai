// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Screen-seam topology — the ONE encoding of which adjacent-screen seams are
// NOT contiguous horizontal walks (roadmap OL-B4).  Before this table the
// same facts lived in three places and had already drifted once: the actual
// warp behaviour (systems/transitions.cpp, capstone-cited — still the
// AUTHORITY for what happens), the widescreen peek suppression
// (presentation/widescreen.cpp), and the transition-kind classification
// (game_app's l3_trunk_descent / l7_fake_cave checks).  The latter two now
// DERIVE from this table; transitions.cpp keeps its cited constants and the
// unit test cross-checks the table against them.
//
// Follow-on (deliberately not done here): externalise to a shared JSON
// catalog like menus.json once the Python port needs the same data.
//
// EXE evidence per seam is cited inline.  Everything not listed is a
// contiguous walk (or out of range).

#pragma once

namespace olduvai::systems {

enum class SeamKind {
    Contiguous,        // normal horizontal walk (pan transition)
    Warp,              // teleport/cave-warp — screens not spatially adjacent
    TrunkDescent,      // L3 17→18: the descent animation IS the transition
    FakeCaveInstant,   // L7 12↔13: EXE instant warp (capstone 25b2:07df
                       // jumps over the wipe); enhanced renders a fade
};

// Seam between surface screens `a` and `b` of `internal_level` (order
// irrelevant; only |a-b| == 1 seams are meaningful — anything else returns
// Contiguous and callers must not ask).
inline SeamKind seam_kind(int internal_level, int a, int b) {
    const int lo = a < b ? a : b;
    const int hi = a < b ? b : a;
    if (hi - lo != 1) return SeamKind::Contiguous;
    if (internal_level == 3) {
        // Dark Woods trunk pocket: S9 right-edge cave-warp → S10; the 10|11
        // interior halves are a vertical climb, not a walk; S11 top → S12
        // exit warp (see check_l3 transition code + Findings
        // l3_s10_s11_cave_palette_persists / trunk warp notes).
        if (lo == 9 || lo == 10 || lo == 11) return SeamKind::Warp;
        // Level-end giant-trunk descent (FUN_2276_03d9): vertical, down.
        if (lo == 17) return SeamKind::TrunkDescent;
    }
    if (internal_level == 7) {
        // Volcanic cave hall: S9 right edge clamps + cave-DESCENT warps to
        // S10 (10,131) — check_l7_transition.
        if (lo == 9) return SeamKind::Warp;
        // S12 right edge TELEPORTS to S13 (48,130); EXE entry is instant
        // (capstone 25b2:07df), classic pans (documented simplification),
        // enhanced fades (Findings/l7_fake_cave_no_fade_in_exe.md).
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
