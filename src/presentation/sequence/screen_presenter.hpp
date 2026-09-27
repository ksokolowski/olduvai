// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The PresentFn both drivers hand to the non-gameplay screens (loading card,
// tally, fades, transitions, L3 descent):
//     poll -> compose -> [gate dump] -> present -> pace
// Only `compose` differs per driver.  Pacing absorbs the compose cost into the
// frame budget (a flat delay on top made the L3 descent ~11 fps at omniscale
// x4).
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
    // gate dump will read the backbuffer (black on Metal after a present).
    using ComposeFn = std::function<void(const FrameBuffer&, bool do_present)>;

    ScreenPresenter(LevelSurface& surface, ComposeFn compose, Uint32 frame_ms)
        : surface_(&surface),
          compose_(std::move(compose)),
          frame_ms_(frame_ms) {}

    // Name the screen about to be presented, so each gates and counts its own
    // frames.  Null env = not gating.
    void begin_screen(const char* dump_env, const char* dump_tag) {
        dump_env_ = dump_env;
        dump_tag_ = dump_tag;
        dump_seq_ = 0;
    }
    void end_screen() { dump_env_ = nullptr; }

    // Run one text screen: build its HD text handle when vector text is on,
    // name it for the gate, run `body`, clear the name.  Returns body's result
    // (false = the player quit).
    bool text_screen(const TextScreenDeps& deps, bool hd_ok,
                     const char* dump_env, const char* dump_tag,
                     const std::function<bool(const TextScreenHd&)>& body) {
        TextScreenHd hd;
        if (hd_ok) hd = make_text_screen_hd(deps, dump_env, dump_tag);
        begin_screen(dump_env, dump_tag);
        const bool ok = body(hd);
        end_screen();
        return ok;
    }

    bool operator()(const FrameBuffer& f) {
        const Uint32 t0 = SDL_GetTicks();
        // ESC is inert on these screens (no menu; a won fight must not become a
        // game over).  The poll still drains, so keys from the previous screen
        // cannot leak in.
        if (!poll_screen_events(surface_->win())) return false;
        const bool gating =
            dump_env_ != nullptr && std::getenv(dump_env_) != nullptr;
        compose_(f, /*do_present=*/!gating);
        if (gating) {
            const bool more =
                capture_gate_frame(surface_->ren(), dump_env_, dump_tag_,
                                   dump_seq_);
            present_output(surface_->ren());
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
