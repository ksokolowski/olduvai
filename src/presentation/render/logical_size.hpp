// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SDL's logical size and the mirror that restores it, as one thing.
//
// WHY THIS EXISTS (BACKLOG §3.13).  `TextOverlay::flush()` disables logical
// scaling, copies the overlay at output resolution, and then RESTORES the
// logical size from the two ints it was handed.  So those ints are not a record
// of the logical size — for every frame after an overlay draw, they ARE it.
// A `SDL_RenderSetLogicalSize` that does not also update them is undone by the
// next flush.
//
// That invariant was maintained by hand at eight sites across the two drivers,
// and one of them lost it: the boss score tally set SDL to the pillarbox dims
// and its own HD-text flush put the fight's WIDE dims back a frame later,
// displacing the tally by exactly the pillarbox margin (128 px in an 896x400
// window — confirmed against the classic non-widescreen tally, which lands
// where the fixed one does).
//
// Here the only way to change the logical size also updates the mirror, so the
// eight sites are correct by construction rather than by everyone remembering.
// Same move as §3.4's `boss_visual_target` made for `advance_state`: turn a
// remembered invariant into a structural one.
#pragma once

#include <SDL.h>

namespace olduvai::presentation {

class LogicalSize {
public:
    LogicalSize(SDL_Renderer* ren, int w, int h) : ren_(ren), w_(w), h_(h) {}

    // The ONLY way to change it.  Writes SDL and the mirror together.
    void set(int w, int h) {
        w_ = w;
        h_ = h;
        SDL_RenderSetLogicalSize(ren_, w_, h_);
    }

    // Push the current value at SDL without changing it — for the points that
    // used to call SDL_RenderSetLogicalSize with what the mirror already held.
    void apply() const { SDL_RenderSetLogicalSize(ren_, w_, h_); }

    int w() const { return w_; }
    int h() const { return h_; }

private:
    SDL_Renderer* ren_;
    int w_;
    int h_;
};

}  // namespace olduvai::presentation
