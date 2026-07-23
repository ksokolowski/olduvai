// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Shared five-key menu navigation: arrows/WASD move and adjust, Enter or
// Space activates.  This dispatch existed verbatim at the three keydown
// sites (surface pause, title menu, boss pause) — unified here so a new
// binding cannot be added to one menu and forgotten in the others (CC3
// phase 4, slice 1).
//
// ESC deliberately stays with the caller: its meaning differs per site
// (pause closes at the root, the title menu stays at the root, the boss
// overlay closes back into the fight) and unifying it would flatten real
// behaviour differences, not duplication.

#pragma once

#include <SDL.h>

#include "presentation/menu.hpp"

namespace olduvai::presentation {

// Returns true when the key was one of the five navigation keys (the menu
// was told to move/adjust/activate), false for anything else.
inline bool menu_nav_keydown(Menu& m, SDL_Keycode sym) {
    if (sym == SDLK_UP || sym == SDLK_w)              m.move(-1);
    else if (sym == SDLK_DOWN || sym == SDLK_s)       m.move(+1);
    else if (sym == SDLK_LEFT || sym == SDLK_a)       m.adjust(-1);
    else if (sym == SDLK_RIGHT || sym == SDLK_d)      m.adjust(+1);
    else if (sym == SDLK_RETURN || sym == SDLK_SPACE) m.activate();
    else return false;
    return true;
}

}  // namespace olduvai::presentation
