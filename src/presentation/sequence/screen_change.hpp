// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Step 9 of docs/FRAME_LOOP.md and the transition it plays: classify the
// screen change (surface pan, fade pair, enhanced secret slides), capture the
// outgoing frame before the rebind, bind the new screen, and play the effect
// after the new screen's first compose.  The L3 trunk descent (17 -> 18)
// runs its own cinematic instead.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "presentation/level/level_state.hpp"       // Loaded
#include "presentation/render/game_render.hpp"      // FrameBuffer, RenderTarget
#include "presentation/sequence/l3_end_level.hpp"   // DescentCtx
#include "presentation/sequence/secret_slide.hpp"   // SlideLanding
#include "presentation/sequence/transition_classify.hpp"   // PrevFrame
#include "presentation/sequence/transition_geometry.hpp"   // TransitionKind
#include "systems/player.hpp"

namespace olduvai::presentation {

struct FramePresenter;

struct TransitionState {
    explicit TransitionState(int w, int h) : old_frame(w, h) {}
    // HD-sized like the gameplay fb: the outgoing frame, captured before the
    // rebind and played back after the new screen's first compose.
    FrameBuffer old_frame;
    TransitionKind kind = TransitionKind::kNone;
    char dir = 'R';

    // Wide transition (ws_present_path screen): the whole transition presents
    // at the wide width so bars and HUD do not pop mid-pan/fade.  `old_wide` is
    // the outgoing frame composed wide before the rebind; the incoming one is
    // composed after it.  `wide` false = the 320 fp.present path.
    std::vector<std::uint8_t> old_wide;
    bool wide = false;

    // The secret slides' wide outgoing frame, built at capture: the exit's
    // needs the room as it was left, restored before the pan/fade wide block
    // runs.
    std::vector<std::uint8_t> slide_old_wide;
    bool slide_old_wide_ok = false;

    SlideLanding landing;   // the secret exit's arc, saved at capture
};

// Step 9: the screen changed this frame.  `descent` is the driver's context
// for the L3 trunk descent; its previous screen and logical size are filled
// here.
void change_screen(Loaded& g, const PrevFrame& pf, TransitionState& trans,
                   FramePresenter& fp, FrameBuffer& fb, bool enhanced,
                   DescentCtx descent);

// The run-loop state the blocking players need.
struct TransitionEnv {
    bool* running;           // a window close inside a player stops the app
    std::FILE* draw_log;     // OLDUVAI_DRAW_LOG, or null
    std::uint32_t frame_ms;
    bool smooth_motion;
};

// Play the classified transition (trans.kind != 0), then clear it.
void play_screen_transition(Loaded& g, const PrevFrame& pf,
                            TransitionState& trans, FramePresenter& fp,
                            FrameBuffer& fb,
                            const std::function<void(RenderTarget&)>& bubbles,
                            const TransitionEnv& env);

}  // namespace olduvai::presentation
