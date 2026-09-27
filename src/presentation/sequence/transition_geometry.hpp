// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Where the outgoing and incoming screens sit during a transition, shared by
// play_transition (320-wide, possibly HD) and play_transition_wide.  Units
// are the caller's: play_transition passes its buffer size (HD when the buffer
// is), play_transition_wide passes native size (its blitter scales).  t in
// [0, 1], clamped by the callers.  Pure.
#pragma once

// Explicit: libc++ includes these transitively, libstdc++ does not.
#include <algorithm>
#include <cmath>

namespace olduvai::presentation {

// A screen change's transition.  The values number the dump files
// (trans_k<N>_NNNN.bmp): keep them.
enum class TransitionKind {
    kNone = 0,
    kPan = 1,           // surface to surface, in the pan's direction
    kFadePair = 2,      // cave, secret or warp: fade out, then in
    kSecretEntry = 3,   // enhanced: the slide down into the secret room
    kSecretExit = 4,    // enhanced: the slide up out of it, the player arcs
};

inline bool is_secret_slide(TransitionKind k) {
    return k == TransitionKind::kSecretEntry ||
           k == TransitionKind::kSecretExit;
}

// Offsets for the OLD buffer (o*) and the NEW one (n*), in caller units.
struct TransitionShift {
    int odx = 0;
    int ody = 0;
    int ndx = 0;
    int ndy = 0;
};

// The secret slides take their direction from the kind (entry: old slides
// up; exit: old slides down), `dir` unused.  Other kinds pan in `dir`: 'R' /
// 'L' horizontal, 'D' / 'U' vertical ('U' by default).
inline TransitionShift transition_shift(TransitionKind kind, char dir,
                                        double t, int w, int h) {
    TransitionShift s;
    const int dx = static_cast<int>(t * w);
    const int dy = static_cast<int>(t * h);
    if (kind == TransitionKind::kSecretEntry) {   // old UP, new from below
        s.ody = -dy;
        s.ndy = h + s.ody;
        return s;
    }
    if (kind == TransitionKind::kSecretExit) {    // old DOWN, new from above
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

// ---- The kind-4 exit arc's overlay position ----
// Pan phase: the sprite is baked into the incoming surface at its bottom and
// rides in with it (screen y = bake_y + the 'U' pan offset).  Arc phase: the
// surface is static and the player lands from the bake point on the resume
// position:
//     x(t) = sx + (ex - sx) * t
//     y(t) = lerp(bake_y, ey, t) - peak * 4t(1-t)
// At t == 1 it is exactly (ex, ey): no end snap.
struct ArcOverlay {
    int x;
    int y;
};

// The arc's geometry: the two phase lengths (pan, then arc), the start x,
// the resume position, the bake row and the arc's peak.  Native, pre-scale.
struct ArcPath {
    int pan_frames;
    int arc_frames;
    int sx;
    int ex;
    int ey;
    int bake_y;
    double peak;
};

// `pos` is the frame cursor in pan-frame units (fractional when smooth), `t`
// the pan's own 0..1 progress.
inline ArcOverlay arc_overlay_pos(const ArcPath& a, double pos, double t) {
    const int pan_frames = a.pan_frames, arc_frames = a.arc_frames;
    const int sx = a.sx, ex = a.ex, ey = a.ey, bake_y = a.bake_y;
    const double peak = a.peak;
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
