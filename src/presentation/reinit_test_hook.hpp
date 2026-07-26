// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OLDUVAI_REINIT_TEST headless integration hook (driven by reinit_smoke.sh):
// on gameplay frame 5 it forces a render_scale reinit, then on the post-reinit
// re-entry writes a before/after output-size + player-pos + entity round-trip
// checksum to the env-var path and stops the loop.  Env-gated — a total no-op
// in normal play and the oracle trace.  Lifted out of run_platform_level
// (game_app.cpp) as pure test scaffolding; the round-trip latches persist
// across the two run_platform_level calls via file-static state.
#pragma once

#include <SDL.h>

#include "presentation/game_app.hpp"      // GameOptions
#include "presentation/level_save.hpp"    // PendingReinit
#include "presentation/pause_service.hpp" // PauseService
#include "systems/player.hpp"             // systems::SystemsState

namespace olduvai::presentation {

class ReinitTestHook {
public:
    // `path` = getenv("OLDUVAI_REINIT_TEST") (null → the hook is inert).
    explicit ReinitTestHook(const char* path) : path_(path) {}

    // Pre-loop, on the post-reinit re-entry: if the frame-5 trigger fired on the
    // previous run_platform_level call, write the round-trip result file and
    // clear `running` to end the level loop.
    void maybe_write_result(const systems::SystemsState& state,
                            SDL_Window* win, bool& running);

    // In-loop frame-5 trigger: snapshot the pre-reinit state, seed the reinit
    // request, and raise want_reinit + open pause so the loop returns
    // kReinitDisplay.
    void maybe_trigger(const systems::SystemsState& state,
                       const GameOptions& opts, int frame, bool menu_ok,
                       PendingReinit& reinit_req, bool& want_reinit,
                       PauseService& pause);

private:
    const char* path_ = nullptr;
};

}  // namespace olduvai::presentation
