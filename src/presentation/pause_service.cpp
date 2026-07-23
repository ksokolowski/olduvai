// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/pause_service.hpp"

#include <cstdlib>

#include "presentation/dialog_key_map.hpp"
#include "presentation/menu_nav.hpp"
#include "presentation/menu_render.hpp"

namespace olduvai::presentation {

int PauseService::wire_bind_(const External& x) {
    const PauseBindWireDeps wire{x.god_active, x.audio, x.sw, x.opts,
                                 &session_, x.want_reinit, x.reinit_req,
                                 x.logical_w, x.logical_h, x.hd_scale,
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
                    x.display_level},
      bind_wired_(wire_bind_(x)),
      menu_(model, bind_, make_pause_actions(&actions_deps_)),
      flow_deps_{&menu_, x.opts, &bind_, x.reinit_req, x.want_reinit},
      flow_(make_pause_flow(model, session_, confirm_, &flow_deps_)) {}

void PauseService::force_open_screen(const char* screen) {
    menu_.open(screen);
    open_ = menu_.is_open();   // unknown screen id → no overlay
}

void PauseService::begin_frame() {
    if (was_open_ && !open_ && !session_.empty()) flow_.discard();
    was_open_ = open_;
}

void PauseService::esc_pressed() {
    // ESC opens the Pause overlay (Resume / Options / Cheats / Restart /
    // Quit to Title / Quit to Desktop), superseding the bare ESC→game-over.
    // Quit to Title still routes through abort_to_title (the
    // game-over→title path).  Falls back to a direct title-abort if
    // menus.json failed to load.
    if (menu_ok_) {
        open_ = true;
        menu_.open("pause");
    } else {
        *x_.abort_to_title = true;
    }
}

void PauseService::handle_keydown(SDL_Keycode sym) {
    // Confirm dialog intercepts all input while open (§8.6 step 4).
    // SettingsFlow resolves move/apply/discard/cancel through the pause
    // hooks (OL-B1).
    if (confirm_.is_open()) {
        flow_.handle_key(flow_key_from_sym(sym));
        return;
    }
    if (sym == SDLK_ESCAPE) {
        menu_.back();
        if (!menu_.is_open()) open_ = false;
    } else {
        menu_nav_keydown(menu_, sym);
    }
}

void PauseService::track_options_exit() {
    if (open_ && menu_.is_open() && !confirm_.is_open())
        flow_.track_screen(menu_.current_screen());
}

PauseService::FreezeResult PauseService::service_freeze(const FreezeDeps& d) {
    if (!open_) return FreezeResult::kNone;
    if (want_quit_program_) return FreezeResult::kQuitProgram;
    if (want_restart_)      return FreezeResult::kRestartLevel;
    if (want_load_)         return FreezeResult::kLoadCheckpoint;
    if (want_warp_)         return FreezeResult::kWarpLevel;
    if (*x_.want_reinit)    return FreezeResult::kReinitDisplay;
    if (*x_.abort_to_title) return FreezeResult::kAbortGameOver;
    d.g.state.god_mode = d.god_active;   // keep systems in sync if toggled
    FrameBuffer pf{320, 200};
    // Backdrop = the frozen game scene, composed at NATIVE 320×200 (the
    // classic overload, so it works regardless of --hd-profile), then
    // dimmed behind the menu slab.  State is frozen, so re-composing each
    // pause frame is idempotent.
    compose_frame(pf, d.g.state, d.g.render, /*draw_player=*/true);
    if (confirm_.is_open()) {
        // Confirm dialog replaces the menu while open (§8.6 step 5).
        draw_confirm(pf, confirm_, d.g.charset, /*dim=*/true,
                     /*draw_text=*/!d.use_hd_text);
    } else {
        // In HD the glyphs are drawn crisply by the vector overlay
        // (draw_menu_vector in upload_and_show); here draw the slab +
        // accent only.  In classic, draw the bitmap glyphs too.
        draw_menu(pf, menu_, d.g.charset, /*dim=*/true,
                  /*draw_text=*/!d.use_hd_text,   // bitmap unless enhanced
                  d.g.render.entity_sprites.size() > 33
                      ? &d.g.render.entity_sprites[33]
                      : nullptr,    // blank score-bone pointer …
                  &d.g.render.palette);   // … in the level's colours
    }
    d.upload_and_show(pf, /*with_hud=*/false, /*do_present=*/true);
    if (std::getenv("OLDUVAI_PAUSE_SHOT")) {
        // Quit the whole program, not just this level's frame loop —
        // otherwise the sequencer advances to the next level and never
        // exits (the pause_shot hung until timeout).
        return FreezeResult::kShotQuit;
    }
    SDL_Delay(d.frame_ms);
    return FreezeResult::kFroze;
}

}  // namespace olduvai::presentation
