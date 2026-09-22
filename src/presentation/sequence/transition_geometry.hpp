// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Where the outgoing and incoming screens sit during a transition.
//
// The two players — `play_transition` (a 320-wide FrameBuffer, possibly HD)
// and `play_transition_wide` (pre-upscaled wide buffers) — are, in their own
// header's words, "the same kinds over WIDE native buffers".  They differ in
// what they blit and how they present it; they agreed, by writing it out
// twice, on the arithmetic of WHERE the two screens go at progress t.  This
// is that arithmetic, once (BACKLOG §3.12: 106 + 115 points, the largest
// non-driver pair in the tree).
//
// UNITS ARE THE CALLER'S.  `play_transition` passes its buffer size, which is
// HD when the buffer is; `play_transition_wide` passes NATIVE width/height
// because its blitter multiplies by hd_scale itself.  The function only does
// arithmetic, so both are correct — and neither may assume the other's.
//
// t is progress in [0, 1]; the callers clamp it (a smooth-motion frame can
// land past the end of the wall clock).  Pure: no SDL, no state.
#pragma once

// <algorithm> for std::min and <cmath> for std::lround: arc_overlay_pos
// rounds, and libc++ pulls both in transitively where libstdc++ does not
// (the lesson tests/test_transition_geometry.cpp records).
#include <algorithm>
#include <cmath>

namespace olduvai::presentation {

// Offsets for the OLD buffer (o*) and the NEW one (n*), in caller units.
struct TransitionShift {
    int odx = 0;
    int ody = 0;
    int ndx = 0;
    int ndy = 0;
};

// kind 3 / 4 are the secret-room slides, whose direction is implied by the
// kind (3 = entry, old slides UP; 4 = exit, old slides DOWN) — `dir` is not
// read for them.  Every other kind is a surface pan in `dir`:
// 'R' / 'L' horizontal, 'D' / 'U' vertical ('U' is the default, as in both
// players' switch).
inline TransitionShift transition_shift(int kind, char dir, double t, int w,
                                        int h) {
    TransitionShift s;
    const int dx = static_cast<int>(t * w);
    const int dy = static_cast<int>(t * h);
    if (kind == 3) {            // secret entry: old UP, new from the bottom
        s.ody = -dy;
        s.ndy = h + s.ody;
        return s;
    }
    if (kind == 4) {            // secret exit: old DOWN, new from the top
        s.ody = dy;
        s.ndy = s.ody - h;
        return s;
    }
    switch (dir) {
        case 'R':
            s.odx = -dx;
            s.ndx = w + s.odx;
            break;
        case 'L':
            s.odx = dx;
            s.ndx = s.odx - w;
            break;
        case 'D':
            s.ody = -dy;
            s.ndy = h + s.ody;
            break;
        default:                // 'U'
            s.ody = dy;
            s.ndy = s.ody - h;
            break;
    }
    return s;
}

// ── The kind-4 exit arc's overlay position ──────────────────────────────────
//
// The secret-exit transition draws the player twice over its two phases, and
// BOTH players wrote the same arithmetic out (transition_players.cpp, the
// native and the wide loop).  What differs between them is where the sprite
// is blitted, not where it goes; that is this function.
//
// Phase 1 (PAN): the sprite is BAKED into the surface at its bottom and rides
// in with the roll — screen y = bake_y + the 'U' pan's own offset — so it
// never pops to a fixed screen spot.
// Phase 2 (ARC): the surface is static and the player lands from the bake
// point to the resume position, x linear in t and y parabolic with its apex
// at the halfway point:
//     x(t) = sx + (ex - sx) * t
//     y(t) = lerp(bake_y, ey, t) - peak * 4t(1-t)
// Matches the reference and spec §F7.  No end snap: at t == 1 the overlay is
// exactly (ex, ey), which is what the draw-log harness asserts.
struct ArcOverlay {
    int x;
    int y;
};

// `pos` is the frame cursor in pan-frame units (fractional when smooth),
// `t` the pan's own 0..1 progress, `pan_frames` / `arc_frames` the two phase
// lengths.  Coordinates are native, pre-scale.
inline ArcOverlay arc_overlay_pos(double pos, double t, int pan_frames,
                                  int arc_frames, int sx, int ex, int ey,
                                  int bake_y, double peak) {
    ArcOverlay o{};
    if (pos <= pan_frames) {
        o.x = sx;
        o.y = bake_y + static_cast<int>(std::lround((t - 1.0) * 200.0));
        return o;
    }
    const double ta = std::min(
        1.0, (pos - pan_frames) / static_cast<double>(arc_frames));
    o.x = static_cast<int>(std::lround(sx + (ex - sx) * ta));
    const double lin = bake_y + (ey - bake_y) * ta;
    o.y = static_cast<int>(std::lround(lin - peak * 4.0 * ta * (1.0 - ta)));
    return o;
}

}  // namespace olduvai::presentation
