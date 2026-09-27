// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The secret room's slides (enhanced; PARITY T7).  Entry: the surface slides
// up and the room in from below, 12 frames.  Exit: the room slides down and
// the surface in from above, 30 frames, the player riding in baked at the
// surface's bottom; then he arcs from there to where he lands, 26 frames:
// linear x, parabolic y (ArcPath), as the reference.  Classic fades instead
// (classify_transition).
#pragma once

#include <cstdio>
#include <functional>

#include "presentation/sequence/transition_classify.hpp"   // ShownFields
#include "presentation/sequence/transition_geometry.hpp"   // TransitionKind

namespace olduvai::presentation {

struct Loaded;
struct TransitionState;
struct FramePresenter;
struct FrameBuffer;
struct RenderTarget;
struct LevelRenderAssets;
struct TransitionShellCtx;

// Where the exit's arc starts and lands, saved before the rebind moves them.
struct SlideLanding {
    int exit_x = 0;   // where the player left the room (secret_exit_x)
    int end_x = 0;    // where he lands on the surface
    int end_y = 0;
};

class SecretSlide {
public:
    SecretSlide(TransitionKind kind, const SlideLanding& at, bool facing_left);

    int frames() const { return frames_; }   // the slide
    int total() const { return frames_ + arc_frames_; }
    bool has_arc() const { return arc_frames_ > 0; }

    // The exit's player at frame cursor `pos` (pan-frame units, fractional
    // when smooth), into `rt`.  `log`: OLDUVAI_DRAW_LOG, the overlay against
    // the landing — the first frame shows a start pop, the last must equal
    // the landing (no end snap).
    void draw_arc(RenderTarget& rt, const LevelRenderAssets& a, double pos,
                  int f2, std::FILE* log) const;

private:
    static constexpr int kBakeY = 185;   // the surface's bottom
    static constexpr double kArcPeak = 30.0;

    int frames_;
    int arc_frames_;
    SlideLanding at_;
    bool flip_;   // facing from net travel; on a tie the player's own
};

// The exit, before the rebind: where the arc starts and lands.
SlideLanding slide_landing(const systems::SystemsState& st);

// The room as the player left it: the exit cleared secret_flag, but the
// room's assets stay bound until bind_screen.
ShownFields room_as_left(const systems::SystemsState& st,
                         const SlideLanding& at);

// After the new screen's first compose: the slide, wide when both sides
// have a wide frame, else 320.  The new screen is drawn player-less: the
// exit draws its own arc, the entry hides the player mid-slide.
void play_secret_slide(Loaded& g, TransitionState& trans, FramePresenter& fp,
                       FrameBuffer& fb, TransitionShellCtx& tctx,
                       const std::function<void(RenderTarget&)>& bubbles);

}  // namespace olduvai::presentation
