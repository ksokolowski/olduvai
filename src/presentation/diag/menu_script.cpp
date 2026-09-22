// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/diag/menu_script.hpp"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>

#include "presentation/diag/menu_script_util.hpp"
#include "presentation/diag/report_form.hpp"

namespace olduvai::presentation {

void MenuScript::load_from_env() {
    script = parse_menu_script(std::getenv("OLDUVAI_MENU_SCRIPT"));
    if (const char* d = std::getenv("OLDUVAI_MENU_SCRIPT_DIR")) dir = d;
}

bool drive_menu_script(MenuScript& ms, ReportFormService* form) {
    if (ms.idx >= ms.script.size()) {
        ms.quit = true;   // auto-exit at end of script
        return true;
    }
    const std::string tok = ms.script[ms.idx++];
    if (tok == "quit") {
        ms.quit = true;
    } else if (tok == "wait") {
        // idle one iteration
    } else if (tok == "shot") {
        char nm[32];
        std::snprintf(nm, sizeof nm, "%03d.png", ms.shot_ctr++);
        ms.shot_path = ms.dir + "/" + nm;
    } else if (tok.rfind("type:", 0) == 0) {
        // Text-editor typing: '_' → space.  Dispatched STRAIGHT to the
        // editor's event handler, NOT via SDL_PushEvent: sdl2-compat
        // (Homebrew's SDL2 since 2026-07) refuses app-pushed TEXTINPUT
        // events — its Event2to3 returns NULL ("we shouldn't be getting text
        // input events this direction") and SDL3_PushEvent(NULL) segfaults.
        // The direct call reaches the same consumer the poll loop feeds; a
        // TEXTINPUT can only insert (kNone), so the save/cancel handling
        // there is not needed here.
        std::string txt = tok.substr(5);
        for (char& c : txt) if (c == '_') c = ' ';
        if (form != nullptr && form->open() && form->edit_open())
            form->inject_text(txt);
    } else if (tok == "stab" || tok == "ctrlenter") {
        // Modifier chords the plain key-pusher can't express.
        const SDL_Keycode sym = tok == "stab" ? SDLK_TAB : SDLK_RETURN;
        const Uint16 mod = tok == "stab" ? KMOD_LSHIFT : KMOD_LCTRL;
        for (bool down : {true, false}) {
            SDL_Event e{};
            e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
            e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
            e.key.keysym.sym = sym;
            e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
            e.key.keysym.mod = mod;
            SDL_PushEvent(&e);
        }
    } else {
        const SDL_Keycode sym = menu_token_sym(tok);
        if (sym != SDLK_UNKNOWN) push_menu_key(sym);
    }
    return ms.quit;
}

}  // namespace olduvai::presentation
