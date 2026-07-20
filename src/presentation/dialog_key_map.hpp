// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SDL keycode → SettingsFlow::Key bridge for the settings/confirm dialog.
//
// Kept OUT of settings_flow.hpp on purpose: that header is pure logic with
// no SDL dependency (see its top comment, "Same policy, one encoding. Pure
// logic: no SDL"). This is the one place that maps raw SDL keysyms onto the
// semantic dialog keys, shared by the surface (game_app) and boss (boss_app)
// pause/menu flows so the mapping cannot drift.
//
// Previously mirrored verbatim in both TUs (per OL-B6); unified here (CC1
// review, thread CC3) without pulling SDL into the pure-logic layer.

#pragma once

#include <SDL.h>

#include "presentation/settings_flow.hpp"

namespace olduvai::presentation {

inline SettingsFlow::Key flow_key_from_sym(SDL_Keycode sym) {
    if (sym == SDLK_LEFT || sym == SDLK_a || sym == SDLK_UP || sym == SDLK_w)
        return SettingsFlow::Key::kPrev;   // left/up → towards Apply
    if (sym == SDLK_RIGHT || sym == SDLK_d || sym == SDLK_DOWN || sym == SDLK_s)
        return SettingsFlow::Key::kNext;   // right/down → towards Discard
    if (sym == SDLK_RETURN || sym == SDLK_SPACE)
        return SettingsFlow::Key::kAccept;
    if (sym == SDLK_ESCAPE)
        return SettingsFlow::Key::kCancel;
    return SettingsFlow::Key::kNone;
}

}  // namespace olduvai::presentation
