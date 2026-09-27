// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss/boss_pause.hpp"

namespace olduvai::presentation {

void poll_boss_events(SDL_Window* win, ReportFormService& form,
                      BossPause& pause, bool replay_active,
                      BossRunResult& res, bool& running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (handle_fullscreen_toggle(ev, win)) continue;
        if (ev.type == SDL_QUIT) {
            res.quit = true;
            running = false;
        } else if (form.open()) {
            form.handle_event(ev);
        } else if (ev.type == SDL_KEYDOWN) {
            const SDL_Keycode sym = ev.key.keysym.sym;
            if (pause.is_open()) {
                pause.keydown(sym);
            } else if (sym == SDLK_ESCAPE) {
                if (replay_active || !pause.open_screen("pause_boss")) {
                    res.quit = true;
                    running = false;
                }
            } else if (sym == SDLK_F5) {
                form.open_form();   // freezes from this frame
            }
        }
    }
}

}  // namespace olduvai::presentation
