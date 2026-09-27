// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// In-game Pause wiring: the Options flow, the actions, the bindings.

#include "presentation/menu/pause_flow.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

#include <SDL.h>

#include "core/types.hpp"                   // Entity, ObjType, kInitialEnergy
#include "presentation/level/level_save.hpp"      // capture_save
#include "presentation/input/gamepad.hpp"          // printed_family
#include "presentation/menu/profile_table.hpp"    // family_button_layout
#include "presentation/menu/settings_apply.hpp"  // classify_change, ApplyTier, StagedChange
#include "presentation/menu/settings_seed.hpp"
#include "presentation/window_util.hpp"    // window_fullscreen

namespace olduvai::presentation {

namespace {
// Launch the EXE's rising-bonus arc (same path a clubbed ancestor-ghost drop
// takes). Moved verbatim from run_platform_level's cheat_spawn_bonus.
void cheat_spawn_bonus(PauseActionsDeps* d, int bonus_type) {
    if (d->replay->active()) return;
    core::Entity e;
    e.obj_type = core::ObjType::AncestorGhost;
    e.active = true;
    e.visible = true;
    e.mask = 0x80 | bonus_type;
    e.counter = 65;
    e.bonus_rise_dy = -20;
    e.bonus_rise_y = d->g->state.player.y + 10;
    e.x = d->g->state.player.x;
    e.y = e.bonus_rise_y;
    e.prev_x = e.x;
    e.prev_y = e.y;
    d->g->state.entities.push_back(e);
    *d->pause_open = false;   // close the menu so the arc is visible
}
}  // namespace

std::optional<MenuModel> load_menu_model(const std::string& profile_family) {
    // Built at compile time, so it cannot be missing; optional<> keeps the call
    // sites' shape.
    MenuModel m = built_in_menu_model();
    const char* printed = family_button_layout(profile_family);
    const PadFamily fallback = printed != nullptr &&
                                       std::string(printed) == "nintendo"
                                   ? PadFamily::kNintendo
                                   : PadFamily::kXbox;
    label_pad_rows(m, gamepad::printed_family().value_or(fallback));
    return m;
}

SettingsFlow make_pause_flow(MenuModel& model, SettingsSession& session,
                             ConfirmDialog& confirm, PauseFlowDeps* d) {
    SettingsFlow::Hooks h = staging_flow_hooks(*d->bind, session, d->menu);
    h.apply_begin = [d]() {
        // Seed reinit_req from current rt first; staged reinit-class changes
        // override below.
        *d->reinit_req = display_settings_of(*d->opts);
    };
    h.apply_change = [d](const StagedChange& ch, ApplyTier tier) {
        // Apply the new value to the live rt / state.
        if (tier == ApplyTier::Reinit) {
            set_display_key(*d->reinit_req, ch.key, ch.new_value);
            // smooth_motion derives from the umbrella; move it too, or the live
            // options disagree with the reinit being built.
            if (ch.key == "enhanced")
                d->opts->enhance.smooth_motion = d->reinit_req->enhanced;
        }
        // Non-reinit keys: volume/fullscreen already previewed live;
        // hd_profile same-scale → rt.
        if (ch.key == "hd_profile" && tier == ApplyTier::Live &&
            d->bind->live_hd_profile)
            *d->bind->live_hd_profile = ch.new_value;
        // enhance.* flags are adopted into the live GameOptions (most are read
        // per frame; level-entry latches catch up at the next reinit or level).
    };
    h.apply_done = [d](bool needs_reinit) {
        // The driver rebuilds the display in place from the pause block and
        // clears the flag; the pause stays open either way.
        if (needs_reinit) *d->want_reinit = true;
    };
    h.confirm_note = [](bool any_reinit, bool any_persist) {
        if (any_reinit) return std::string("Apply settings now.");
        if (any_persist)
            return std::string("Saved - takes effect on next launch.");
        return std::string{};
    };
    return SettingsFlow(model, session, confirm, std::move(h));
}

MenuActionTable make_pause_actions(PauseActionsDeps* d) {
    return {
        {"resume", [d] { *d->pause_open = false; }},
        {"quit_title", [d] {
            d->confirm->ask("Quit to title?", [d] { *d->abort_to_title = true; });
        }},
        {"quit_desktop", [d] {
            d->confirm->ask("Exit game?", [d] { *d->want_quit_program = true; });
        }},
        {"restart_level", [d] { *d->want_restart = true; }},
        {"cheat_bonus_0", [d] { cheat_spawn_bonus(d, 0); }},
        {"cheat_bonus_1", [d] { cheat_spawn_bonus(d, 1); }},
        {"cheat_bonus_2", [d] { cheat_spawn_bonus(d, 2); }},
        {"cheat_bonus_3", [d] { cheat_spawn_bonus(d, 3); }},
        {"cheat_bonus_4", [d] { cheat_spawn_bonus(d, 4); }},
        {"cheat_bonus_5", [d] { cheat_spawn_bonus(d, 5); }},
        {"cheat_refill", [d] {
            if (d->replay->active()) return;
            d->g->state.player.energy =
                *d->god_active ? 999 : core::kInitialEnergy;
            *d->pause_open = false;
        }},
        {"cheat_warp", [d] {
            if (d->replay->active()) return;
            const std::string v = d->bind->get("cheat.start_level");
            // atoi is safe: menus.json declares cheat.start_level a choice over
            // [1..7] and nothing types into it.  A text field or a persisted
            // value would need a checked parse.
            // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
            const int lvl = v.empty() ? 0 : std::atoi(v.c_str());
            // Leave pause open: the run loop's pause block is the only consumer
            // of want_warp.
            if (lvl >= 1 && lvl <= 7) *d->want_warp = lvl;
        }},
        {"save_game", [d] {
            const SaveState cp = capture_save(*d->g, d->display_level);
            if (d->opts->save_path.empty()) {
                std::fprintf(stderr, "save: no save path configured\n");
            } else if (save_to_file(cp, d->opts->save_path)) {
                std::fprintf(stderr, "save: game written to %s\n",
                             d->opts->save_path.c_str());
            } else {
                // A failed quicksave must not be SILENT (read-only dir, full
                // disk) — the user would believe they have a checkpoint.
                std::fprintf(stderr, "save: FAILED to write %s\n",
                             d->opts->save_path.c_str());
            }
            *d->pause_open = false;
        }},
        {"load_game", [d] {
            if (!d->opts->save_path.empty()) {
                if (auto loaded = load_from_file(d->opts->save_path)) {
                    *d->out_load = loaded;   // run_game re-enters at saved level
                    *d->want_load = true;
                }
            }
        }},
    };
}

void configure_pause_bind(PauseBindings& bind, const PauseBindWireDeps& d) {
    bind.god = d.god_active;
    bind.god_session = &d.opts->god;   // survives the level boundary
    bind.autofire = &d.opts->autofire;
    bind.attach(d.audio, d.sw->win, d.session, *d.opts);
    bind.live_hd_profile = &d.opts->hd_profile;   // opts == run_game's rt
    // Tier-1 live Aspect: the setting only; the presentation follows it
    // before its next present (WidescreenPresenter::sync_output).
    bind.apply_aspect = [opts = d.opts](const std::string& v) {
        opts->aspect = v;
    };
    bind.mem["cheat.start_level"] = std::to_string(d.display_level);
}

}  // namespace olduvai::presentation
