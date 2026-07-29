// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OLDUVAI_MENU_SCRIPT shared plumbing — token parsing and synthetic SDL key
// injection, used by BOTH headless menu-walk consumers: run_platform_level's
// pause/menu walk (tests/menu_script.sh) and run_title_menu's title-menu walk
// (tests/title_style_apply.sh).  The owning loop consumes one token per frame
// and keeps the loop-specific tokens (shot/wait/quit, type:, chords) to
// itself; this header only covers the parts both loops share.

#pragma once

#include <SDL.h>

#include <string>
#include <vector>

namespace olduvai::presentation {

// Split an OLDUVAI_MENU_SCRIPT value into tokens (space/comma/tab/newline
// separated).  Empty input → empty script (walk disabled).
inline std::vector<std::string> parse_menu_script(const char* ms) {
    std::vector<std::string> script;
    if (ms == nullptr) return script;
    std::string t;
    for (char c : std::string(ms)) {
        if (c == ' ' || c == ',' || c == '\t' || c == '\n') {
            if (!t.empty()) { script.push_back(t); t.clear(); }
        } else { t += c; }
    }
    if (!t.empty()) script.push_back(t);
    return script;
}

// Plain-key token → SDL keycode (modifier chords and type: tokens are
// consumer-specific).  SDLK_UNKNOWN = not a plain-key token.
inline SDL_Keycode menu_token_sym(const std::string& t) {
    if (t == "esc")   return SDLK_ESCAPE;
    if (t == "up")    return SDLK_UP;
    if (t == "down")  return SDLK_DOWN;
    if (t == "left")  return SDLK_LEFT;
    if (t == "right") return SDLK_RIGHT;
    if (t == "enter") return SDLK_RETURN;
    if (t == "space") return SDLK_SPACE;
    if (t == "f5")    return SDLK_F5;      // open the bug-report form
    if (t == "bksp")  return SDLK_BACKSPACE;
    if (t == "del")   return SDLK_DELETE;
    if (t == "home")  return SDLK_HOME;
    if (t == "end")   return SDLK_END;
    if (t == "tab")   return SDLK_TAB;
    if (t.size() == 1 && t[0] >= '1' && t[0] <= '6')
        return static_cast<SDL_Keycode>(SDLK_1 + (t[0] - '1'));
    return SDLK_UNKNOWN;
}

// Push one synthetic keydown+keyup pair (the same SDL_PushEvent path the
// gamepad uses).  KEY events only: sdl2-compat refuses app-pushed TEXTINPUT
// events (see game_app's type: token for the direct-dispatch workaround).
inline void push_menu_key(SDL_Keycode sym) {
    for (bool down : {true, false}) {
        SDL_Event e{};
        e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        e.key.keysym.sym = sym;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
        SDL_PushEvent(&e);
    }
}

}  // namespace olduvai::presentation
