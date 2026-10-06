// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss/boss_pause.hpp"

#include "presentation/input/actions.hpp"   // key_is, play_key_is
#include "presentation/input/gamepad.hpp"   // ContextScope

namespace olduvai::presentation {

void poll_boss_events(SDL_Window* win, ReportFormService& form,
                      BossPause& pause, bool replay_active,
                      BossRunResult& res, bool& running) {
    const gamepad::ContextScope ctx(form.open() || pause.is_open()
                                        ? gamepad::Context::kMenu
                                        : gamepad::Context::kPlay);
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (handle_fullscreen_toggle(ev, win)) continue;
        if (ev.type == SDL_QUIT) {
            res.quit = true;
            res.quit_program = true;   // window close, SIGTERM: leave the program
            running = false;
        } else if (form.open()) {
            form.handle_event(ev);
        } else if (ev.type == SDL_KEYDOWN) {
            const SDL_Keycode sym = ev.key.keysym.sym;
            if (pause.is_open()) {
                pause.keydown(sym);
            } else if (play_key_is(ev.key.keysym, Action::kPause)) {
                if (replay_active || !pause.open_screen("pause_boss")) {
                    res.quit = true;
                    running = false;
                }
            } else if (key_is(sym, Action::kBugReport)) {
                form.open_form();   // freezes from this frame
            } else if (!replay_active && play_key_is(ev.key.keysym, Action::kQuit)) {
                pause.quit_shortcut();
            }
        }
    }
}

}  // namespace olduvai::presentation
