// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Live preview of the cheap Options keys (fullscreen, music/sfx volume) —
// the same three branches existed verbatim in the pause, title and boss
// bindings' set() (CC3 phase 4, slice 3).  These preview immediately so
// the user gets feedback while editing; they still stage like every other
// key (persist happens on Apply — the caller keeps the staging call).
//
// The site-specific live keys (hd_profile same-scale swap, Tier-1 aspect)
// deliberately stay with each caller: their targets differ per site
// (rt_hd_profile pointer / GameOptions field / no live path in the boss).

#pragma once

#include <SDL.h>

#include <string>

#include "presentation/audio.hpp"       // SdlAudio
#include "presentation/parse_util.hpp"  // parse_f

namespace olduvai::presentation {

// Returns true when the key was one of the cheap live-preview keys and its
// target was wired (matching the old per-site guards: a null win/audio
// falls through and the key just stages).
inline bool preview_cheap_key(const std::string& k, const std::string& v,
                              SdlAudio* audio, SDL_Window* win,
                              bool enhanced) {
    if (k == "fullscreen" && win) {
        SDL_SetWindowFullscreen(
            win, v == "1" ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        return true;
    }
    if (k == "music_volume" && audio) {
        audio->set_mix_balance(enhanced, parse_f(v, 100.0f) / 100.0f, -1.0f);
        return true;
    }
    if (k == "sfx_volume" && audio) {
        audio->set_mix_balance(enhanced, -1.0f, parse_f(v, 100.0f) / 100.0f);
        return true;
    }
    return false;
}

}  // namespace olduvai::presentation
