// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Shared five-key menu navigation: the Up / Down / Left / Right / Confirm
// actions (input/action_map.hpp: arrows and WASD, Enter, Keypad Enter or
// Space).  This dispatch existed verbatim at the three keydown
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

#include "presentation/input/actions.hpp"   // key_is
#include "presentation/menu/menu.hpp"

namespace olduvai::presentation {

// Returns true when the key was one of the five navigation keys (the menu
// was told to move/adjust/activate), false for anything else.
inline bool menu_nav_keydown(Menu& m, SDL_Keycode sym) {
    if (key_is(sym, Action::kUp))           m.move(-1);
    else if (key_is(sym, Action::kDown))    m.move(+1);
    else if (key_is(sym, Action::kLeft))    m.adjust(-1);
    else if (key_is(sym, Action::kRight))   m.adjust(+1);
    else if (key_is(sym, Action::kConfirm)) m.activate();
    else return false;
    return true;
}

}  // namespace olduvai::presentation
