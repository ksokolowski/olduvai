// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The routing both pause menus share (the surface's PauseService and the
// boss's BossPause): the open state, Discard when closed with staged
// changes, the dialog-first key routing, and the dialog on leaving Options.
#pragma once

#include <SDL.h>

#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/menu/dialog_key_map.hpp"   // menu_dialog_keydown
#include "presentation/menu/menu.hpp"
#include "presentation/menu/settings_flow.hpp"
#include "presentation/menu/settings_session.hpp"

namespace olduvai::presentation {

class PauseRouting {
public:
    // `open` is the owner's flag: its pause actions close it.
    PauseRouting(Menu& menu, SettingsFlow& flow, SettingsSession& session,
                 ConfirmDialog& confirm, bool& open)
        : menu_(menu), flow_(flow), session_(session), confirm_(confirm),
          open_(open) {}

    // Once per frame, first: closed since the last frame with a dirty session
    // = Discard (Apply empties it).
    void begin_frame() {
        if (was_open_ && !open_ && !session_.empty()) flow_.discard();
        was_open_ = open_;
    }
    // A key while open: the dialog first, then ESC one screen out; ESC at the
    // root closes.
    void keydown(SDL_Keycode sym) {
        menu_dialog_keydown(sym, confirm_, flow_, menu_, [this] { open_ = false; });
    }
    // After input: leaving Options with staged changes opens the dialog.
    void track_options_exit() {
        if (open_ && menu_.is_open() && !confirm_.is_open())
            flow_.track_screen(menu_.current_screen());
    }
    // Open on `screen`; an unknown id leaves it closed.
    bool open_screen(const char* screen) {
        menu_.open(screen);
        open_ = menu_.is_open();
        return open_;
    }

private:
    Menu& menu_;
    SettingsFlow& flow_;
    SettingsSession& session_;
    ConfirmDialog& confirm_;
    bool& open_;
    bool was_open_ = false;
};

}  // namespace olduvai::presentation
