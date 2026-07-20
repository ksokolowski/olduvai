// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Intro/title sequence + main menu (the attract, sequence position 0).
//
// Extracted VERBATIM from run_game (CC1, 2026-07-08) — run_game had grown to
// ~1,127 lines with this ~560-line block embedded in it. The block owns the
// publisher logo, title cards, the BULLE dream-hold, and the interactive main
// menu (Start / Continue / Options / Quit), including the title-screen Options
// SettingsFlow that rebuilds the window + audio in place.
//
// Coverage: the compose path is gated by the `mainmenu_shot` ctest; the
// interactive Options -> apply -> rebuild sub-path is NOT automated — verify it
// by playtest after any change here.

#pragma once

#include <functional>
#include <optional>

#include "presentation/audio.hpp"        // SdlAudio
#include "presentation/game_app.hpp"     // GameOptions
#include "presentation/save_state.hpp"   // SaveState
#include "presentation/window_util.hpp"  // ScaledWindow

namespace olduvai::presentation {

// Everything run_title_menu reads or mutates in run_game's scope. References,
// so the title-screen in-place window/audio rebuild is visible to the caller.
struct TitleMenuCtx {
    ScaledWindow& sw;
    GameOptions& rt;                     // mutable session copy (rt = opts)
    std::optional<SdlAudio>& audio_opt;
    const GameOptions& opts;
    bool& hd;
    int& hd_scale;
    int& display;                        // in: 0 (attract); out: Continue retargets it
    bool& quit_requested;                // out
    std::optional<SaveState>& menu_continue;  // out: set by Continue
    bool autoloaded;
    // run_game-local closures the settings-apply path needs.
    std::function<bool(int)> rebuild_window;      // rebuild the window at a new hd_scale
    std::function<void(SdlAudio&)> load_all_sfx;  // (re)load SFX after an audio rebuild
};

void run_title_menu(TitleMenuCtx& ctx);

}  // namespace olduvai::presentation
