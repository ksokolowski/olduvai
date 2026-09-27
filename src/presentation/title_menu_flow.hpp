// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Intro/title sequence + main menu (the attract, sequence position 0).
//
// Owns the publisher logo, title cards, the BULLE dream-hold, and the
// interactive main menu (Start / Continue / Options / Quit), including the
// title-screen Options SettingsFlow that rebuilds the window + audio in place.
//
// Coverage: `mainmenu_shot` gates the compose path, `title_style_apply` the
// Options -> Apply -> rebuild path; the audio-device rebuild is untested.

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "presentation/audio/audio.hpp"        // SdlAudio
#include "presentation/game_app.hpp"     // GameOptions
#include "presentation/level/save_state.hpp"   // SaveState
#include "presentation/pipeline.hpp"

namespace olduvai::presentation {

// Everything run_title_menu reads or mutates in run_game's scope. References,
// so the title-screen in-place window/audio rebuild is visible to the caller.
struct TitleMenuCtx {
    Pipeline& pipe;                      // window, audio, adopt
    GameOptions& rt;                     // mutable session copy (rt = opts)
    const GameOptions& opts;
    bool& hd;
    int& hd_scale;
    int& display;                        // in: 0 (attract); out: Continue retargets it
    bool& quit_requested;                // out
    std::optional<SaveState>& menu_continue;  // out: set by Continue
    bool autoloaded;
};

void run_title_menu(TitleMenuCtx& ctx);

}  // namespace olduvai::presentation
