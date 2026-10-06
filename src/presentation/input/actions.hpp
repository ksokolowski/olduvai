// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The keyboard half of the action table (action_map.hpp), resolved to SDL.
// Screens and menus ask key_is, which reads the fixed defaults, so the
// keyboard always works in a menu.  Play reads the player's bindings (the
// key_* settings): key_held for held state, play_key_is for a press.
#pragma once

#include <SDL.h>

#include <string>

#include "presentation/input/action_map.hpp"

namespace olduvai::presentation {

struct GameOptions;   // (game_app.hpp) — for init_key_bindings

// Whether a key press `sym` is bound to `a` by default.
bool key_is(SDL_Keycode sym, Action a);
// Whether a key press in play is bound to `a` by the player.
bool play_key_is(const SDL_Keysym& key, Action a);
// Whether any key the player binds to `a` is held now.
bool key_held(Action a);

// The player's bindings from a GameOptions' key_* settings.
void init_key_bindings(const GameOptions& opts);
// The live value of a key_* setting; "" for another key.
std::string key_binding(const std::string& key);
// Bind a key_* setting from now on ("" = the action's defaults).  False for
// another key, or when no name in `value` is a key (the binding stays).
bool apply_key_binding(const std::string& key, const std::string& value);

}  // namespace olduvai::presentation
