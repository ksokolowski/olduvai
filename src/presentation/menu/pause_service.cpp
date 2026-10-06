// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/menu/pause_service.hpp"

namespace olduvai::presentation {

int PauseService::wire_bind_(const External& x) {
    const PauseBindWireDeps wire{x.god_active, x.pipe->audio.get(),
                                 &x.pipe->sw, x.opts, &session_,
                                 x.display_level};
    configure_pause_bind(bind_, wire);
    return 0;
}

PauseService::PauseService(MenuModel& model, bool menu_ok, const External& x)
    : x_(x),
      menu_ok_(menu_ok),
      actions_deps_{x.g,           x.replay,        &bind_,
                    x.opts,        x.out_load,      &open_,
                    x.abort_to_title, &want_quit_program_, &want_restart_,
                    &want_load_,   x.god_active,    &want_warp_,
                    x.display_level, &confirm_},
      bind_wired_(wire_bind_(x)),
      menu_(model, bind_, make_pause_actions(&actions_deps_)),
      flow_deps_{&menu_, x.opts, &bind_, x.reinit_req, x.want_reinit},
      flow_(make_pause_flow(model, session_, confirm_, &flow_deps_)) {}

void PauseService::pipeline_changed() {
    bind_.rebind(x_.pipe->audio.get(), x_.pipe->sw.win,
                 display_settings_of(*x_.opts));
}

void PauseService::force_open_screen(const char* screen) {
    routing_.open_screen(screen);   // an unknown id opens no overlay
}

void PauseService::begin_frame() { routing_.begin_frame(); }

void PauseService::esc_pressed() {
    // ESC opens Pause (Resume / Options / Cheats / Restart / Quit).  Quit to
    // Title goes through abort_to_title; without menus.json, abort to the title
    // directly.
    if (menu_ok_) {
        open_ = true;
        menu_.open("pause");
    } else {
        *x_.abort_to_title = true;
    }
}

void PauseService::quit_shortcut() {
    if (!menu_ok_) return;
    open_ = true;
    menu_.open("pause");   // the dialog is drawn over the pause menu
    make_pause_actions(&actions_deps_).at("quit_desktop")();
}

void PauseService::quicksave() {
    const auto actions = make_pause_actions(&actions_deps_);
    actions.at("save_game")();
    if (x_.pipe->audio) x_.pipe->audio->play_sfx("SFX_GENERIC");
}

void PauseService::quickload() {
    const auto actions = make_pause_actions(&actions_deps_);
    actions.at("load_game")();
    if (want_load_) open_ = true;   // verdict() reads intents only while open
}

void PauseService::handle_keydown(SDL_Keycode sym) {
    routing_.keydown(sym);
}

void PauseService::track_options_exit() {
    routing_.track_options_exit();
}

PauseService::FreezeResult PauseService::verdict() const {
    if (!open_) return FreezeResult::kNone;
    if (want_quit_program_) return FreezeResult::kQuitProgram;
    if (want_restart_)      return FreezeResult::kRestartLevel;
    if (want_load_)         return FreezeResult::kLoadCheckpoint;
    if (want_warp_)         return FreezeResult::kWarpLevel;
    if (*x_.want_reinit)    return FreezeResult::kReinitDisplay;
    if (*x_.abort_to_title) return FreezeResult::kAbortGameOver;
    x_.g->state.god_mode = *x_.god_active;   // a Cheats toggle, into systems
    return FreezeResult::kFroze;
}

}  // namespace olduvai::presentation
