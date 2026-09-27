// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OLDUVAI_REINIT_TEST headless integration hook (driven by reinit_smoke.sh):
// on gameplay frame 5 it forces a render_scale reinit, then after the
// in-place rebuild writes a before/after output-size + player-pos + entity
// round-trip checksum to the env-var path and stops the loop.  Env-gated — a
// total no-op in normal play and the oracle trace.
#pragma once

#include <SDL.h>

#include "presentation/game_app.hpp"      // GameOptions
#include "presentation/menu/settings_apply.hpp"   // DisplaySettings
#include "presentation/menu/pause_service.hpp" // PauseService
#include "systems/player.hpp"             // systems::SystemsState

namespace olduvai::presentation {

class ReinitTestHook {
public:
    // `path` = getenv("OLDUVAI_REINIT_TEST") (null → the hook is inert).
    explicit ReinitTestHook(const char* path) : path_(path) {}

    // After the in-place rebuild: if the frame-5 trigger fired, write the
    // round-trip result file and clear `running` to end the level loop.
    // `opts` is the session copy AFTER the adopt — its enhanced/smooth_motion
    // are the live present-path derivation, which the gate must observe (the
    // shipped smooth-motion-after-classic bug lived here and was invisible
    // while only sizes and positions were reported).
    void maybe_write_result(const systems::SystemsState& state,
                            SDL_Window* win, const GameOptions& opts,
                            bool& running);

    // In-loop frame-5 trigger: snapshot the pre-reinit state, seed the reinit
    // request, and raise want_reinit + open pause so the pause's verdict is
    // kReinitDisplay.
    void maybe_trigger(const systems::SystemsState& state,
                       const GameOptions& opts, int frame, bool menu_ok,
                       DisplaySettings& reinit_req, bool& want_reinit,
                       PauseService& pause);

private:
    const char* path_ = nullptr;
};

}  // namespace olduvai::presentation
