// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SDL's logical size and the mirror that restores it, as one thing.
//
// The overlay flush turns logical scaling off and restores the size from the
// ints it is handed (LogicalScalingOff), so after any overlay draw those ints
// ARE the logical size: a SDL_RenderSetLogicalSize that does not update them
// is undone by the next flush.  Here the only way to change the size updates
// both.
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
    // call SDL_RenderSetLogicalSize with what the mirror already holds.
    void apply() const { SDL_RenderSetLogicalSize(ren_, w_, h_); }

    int w() const { return w_; }
    int h() const { return h_; }

private:
    SDL_Renderer* ren_;
    int w_;
    int h_;
};

// Logical scaling off for a scope, so one unit is one output pixel: a copy of
// an output-resolution texture, or a readback of the whole target
// (SDL_RenderReadPixels(nullptr) reads the VIEWPORT, which a logical size
// letterboxes).  The size comes back at scope end: `w` x `h`, or what SDL
// held.
class LogicalScalingOff {
public:
    LogicalScalingOff(SDL_Renderer* ren, int w, int h)
        : ren_(ren), w_(w), h_(h) {
        SDL_RenderSetLogicalSize(ren_, 0, 0);
    }
    explicit LogicalScalingOff(SDL_Renderer* ren) : ren_(ren) {
        SDL_RenderGetLogicalSize(ren_, &w_, &h_);
        SDL_RenderSetLogicalSize(ren_, 0, 0);
    }
    ~LogicalScalingOff() { SDL_RenderSetLogicalSize(ren_, w_, h_); }
    LogicalScalingOff(const LogicalScalingOff&) = delete;
    LogicalScalingOff& operator=(const LogicalScalingOff&) = delete;

private:
    SDL_Renderer* ren_;
    int w_ = 0;
    int h_ = 0;
};

}  // namespace olduvai::presentation
