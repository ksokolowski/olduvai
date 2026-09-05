// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// ScreenPresenter — the PresentFn both drivers hand to the non-gameplay
// screens: the loading card, the score tally, the fades, the transitions and
// the L3 descent.
//
// BACKLOG §3.7 cluster 3, first slice.  Both drivers had this as a lambda over
// their prologue — `present` in game_app, `lpresent` in boss_app — and both are
// the same five steps:
//
//     poll  ->  compose  ->  [gate dump]  ->  present  ->  pace
//
// Only `compose` genuinely differs, so only `compose` is a parameter.  §3.14b
// refused to merge these while that compose difference was tangled up with
// eight surface members each; with LevelSurface owning those, what is left is
// one type with two compose steps, which is the honest version of that merge.
//
// PACING IS NOW THE SAME ON BOTH, AND THAT IS A FIX.  game_app absorbed the
// compose cost into the frame budget; boss_app slept a flat 1000/18 on top of
// it.  The absorbing form exists because an unconditional delay made the L3
// descent ~91 ms a frame at omniscale x4 (~11 fps, the stutter that prompted
// it).  boss_app's screens are the same whole-frame upscales — the loading
// card, the post-win fade, the classic tally — so they carried the same
// latency, unnoticed because they are short.  One screen, two stacks, two
// behaviours: the §3.14 pattern once more, in the pacing this time.
#pragma once

#include <cstdint>
#include <functional>

#include <SDL.h>

#include "presentation/render/game_render.hpp"       // FrameBuffer
#include "presentation/render/level_surface.hpp"
#include "presentation/sequence/text_screen_present.hpp"  // capture_gate_frame
#include "presentation/window_util.hpp"              // poll_screen_events

namespace olduvai::presentation {

class ScreenPresenter {
public:
    // `compose(frame, do_present)` draws one frame and presents it unless the
    // gate dump is about to read the backbuffer — Metal reads black after a
    // present, so in dump mode the presenter presents by hand instead.
    using ComposeFn = std::function<void(const FrameBuffer&, bool do_present)>;

    ScreenPresenter(LevelSurface& surface, ComposeFn compose, Uint32 frame_ms)
        : surface_(&surface),
          compose_(std::move(compose)),
          frame_ms_(frame_ms) {}

    // Name the screen about to be presented, so the gate can attribute its
    // frames and each screen counts its own.  Null env = not gating.
    void begin_screen(const char* dump_env, const char* dump_tag) {
        dump_env_ = dump_env;
        dump_tag_ = dump_tag;
        dump_seq_ = 0;
    }
    void end_screen() { dump_env_ = nullptr; }

    bool operator()(const FrameBuffer& f) {
        const Uint32 t0 = SDL_GetTicks();
        // ESC is inert on every screen this drives — none has a menu wired, and
        // aborting here used to drop a WON fight to game-over.  The poll still
        // DRAINS, so keys mashed on the previous screen cannot leak in.
        if (!poll_screen_events(surface_->win())) return false;
        const bool gating =
            dump_env_ != nullptr && std::getenv(dump_env_) != nullptr;
        compose_(f, /*do_present=*/!gating);
        if (gating) {
            const bool more =
                capture_gate_frame(surface_->ren(), surface_->lsz(), dump_env_,
                                   dump_tag_, dump_seq_);
            SDL_RenderPresent(surface_->ren());
            if (!more) return false;
        }
        // Pace by ABSORBING the compose cost, not by adding to it.
        const Uint32 elapsed = SDL_GetTicks() - t0;
        if (elapsed < frame_ms_) SDL_Delay(frame_ms_ - elapsed);
        return true;
    }

    // std::function view for the show_* screens, which take a PresentFn.
    PresentFn fn() {
        return [this](const FrameBuffer& f) { return (*this)(f); };
    }

private:
    LevelSurface* surface_;
    ComposeFn compose_;
    Uint32 frame_ms_;
    const char* dump_env_ = nullptr;
    const char* dump_tag_ = "none";
    int dump_seq_ = 0;
};

}  // namespace olduvai::presentation
