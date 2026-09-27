// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The boss fight's pause menu and its event poll.
#pragma once

#include <string>

#include <SDL.h>

#include "presentation/boss_app.hpp"                 // BossRunResult
#include "presentation/diag/report_form.hpp"         // ReportFormService
#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/menu/dialog_key_map.hpp"      // menu_dialog_keydown
#include "presentation/menu/menu.hpp"
#include "presentation/menu/menu_model.hpp"
#include "presentation/menu/pause_flow.hpp"          // load_menu_model
#include "presentation/menu/pause_routing.hpp"
#include "presentation/menu/settings_apply.hpp"
#include "presentation/menu/settings_flow.hpp"
#include "presentation/menu/settings_seed.hpp"
#include "presentation/menu/settings_session.hpp"
#include "presentation/menu/staging_bindings.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::presentation {

class SdlAudio;

// The pipeline a boss-pause Apply adopts.  Unlike the level's, it carries
// the aspect: the widescreen arena is built for one, so it is reinit-class.
struct BossReinit : DisplaySettings {
    std::string aspect = "keep";
};

// The boss pause (menus.json `pause_boss`): Resume / Restart Fight / Quit /
// Options; no saves or cheats mid-fight.  Options preview volume and
// fullscreen live; every pipeline key (aspect included) is reinit-class, and
// an Apply raises wants_reinit() for the driver, which rebuilds the display
// in place and calls pipeline_changed().
class BossPause {
public:
    BossPause(const GameOptions& o, SdlAudio* audio, SDL_Window* win,
              BossRunResult& res, bool& running)
        : opts_(&o),
          model_(load_menu_model(o.profile_family).value_or(MenuModel{})),
          menu_ok_(model_.screens.count("pause_boss") != 0),
          menu_(model_, bind_, actions(res, running)),
          flow_(model_, session_, confirm_, hooks()) {
        bind_.attach(audio, win, &session_, o);
        seed_target();
    }
    ~BossPause() = default;
    BossPause(const BossPause&) = delete;
    BossPause& operator=(const BossPause&) = delete;

    MenuModel& model() { return model_; }
    const Menu& menu() const { return menu_; }
    const ConfirmDialog& confirm() const { return confirm_; }
    bool is_open() const { return open_; }
    void begin_frame() { routing_.begin_frame(); }
    void keydown(SDL_Keycode sym) { routing_.keydown(sym); }
    void track_options_exit() { routing_.track_options_exit(); }
    // Open on `screen`; false without the menu or for an unknown screen.
    bool open_screen(const char* screen) {
        return menu_ok_ && routing_.open_screen(screen);
    }

    // An Apply of a pipeline key: the driver adopts reinit_target().
    bool wants_reinit() const { return want_reinit_; }
    const BossReinit& reinit_target() const { return target_; }
    // After the rebuild: the menu previews into the new audio and window,
    // and compares against the adopted settings.
    void pipeline_changed(SdlAudio* audio, SDL_Window* win) {
        bind_.rebind(audio, win, display_settings_of(*opts_));
        want_reinit_ = false;
        seed_target();
    }

private:
    MenuActionTable actions(BossRunResult& res, bool& running) {
        return {
            {"resume", [this] { open_ = false; }},
            {"restart_level", [&res, &running] {
                res.restart = true;
                running = false;
            }},
            {"quit_title", [this, &res, &running] {
                confirm_.ask("Quit to title?", [&res, &running] {
                    res.quit = true;
                    running = false;
                });
            }},
            {"quit_desktop", [this, &res, &running] {
                confirm_.ask("Exit game?", [&res, &running] {
                    res.quit = true;
                    res.quit_program = true;
                    running = false;
                });
            }},
        };
    }

    void seed_target() {
        static_cast<DisplaySettings&>(target_) = display_settings_of(*opts_);
        target_.aspect = opts_->aspect;
    }

    SettingsFlow::Hooks hooks() {
        SettingsFlow::Hooks h = staging_flow_hooks(bind_, session_, &menu_);
        // Every pipeline key is Reinit here, even ones Live on the surface.
        h.classify = [this](const std::string& k, const std::string& v) {
            if (k == "hd_profile" || k == "render_scale" || k == "enhanced" ||
                k == "music_device" || k == "sfx_backend" || k == "aspect")
                return ApplyTier::Reinit;
            return classify_staged(bind_, session_, k, v);
        };
        // Volume/fullscreen were previewed live; enhance.* only persist.
        h.apply_begin = [this] { seed_target(); };
        h.apply_change = [this](const StagedChange& ch, ApplyTier) {
            if (ch.key == "aspect")
                target_.aspect = ch.new_value;
            else
                set_display_key(target_, ch.key, ch.new_value);
        };
        h.apply_done = [this](bool needs_reinit) {
            if (needs_reinit) want_reinit_ = true;
        };
        h.confirm_note = [](bool any_reinit, bool any_persist) {
            if (any_reinit) return std::string("Apply settings now.");
            if (any_persist)
                return std::string("Saved - takes effect on next launch.");
            return std::string{};
        };
        return h;
    }

    const GameOptions* opts_;
    MenuModel model_;
    bool menu_ok_;
    StagingBindings bind_;
    SettingsSession session_;
    ConfirmDialog confirm_;
    BossReinit target_;
    bool want_reinit_ = false;
    Menu menu_;
    SettingsFlow flow_;
    bool open_ = false;
    PauseRouting routing_{menu_, flow_, session_, confirm_, open_};
};

// One frame's events.  The F5 form, then the pause, own input while open; ESC
// opens the pause, and replay or a missing menu aborts to the title instead.
void poll_boss_events(SDL_Window* win, ReportFormService& form,
                      BossPause& pause, bool replay_active,
                      BossRunResult& res, bool& running);

}  // namespace olduvai::presentation
