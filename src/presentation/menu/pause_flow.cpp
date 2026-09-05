// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// make_pause_flow — extracted verbatim (modulo deps-pointer rewrite) from
// run_platform_level's pause SettingsFlow::Hooks. See pause_flow.hpp.

#include "presentation/menu/pause_flow.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

#include <SDL.h>

#include "core/types.hpp"                   // Entity, ObjType, kInitialEnergy
#include "presentation/level/level_save.hpp"      // capture_save
#include "presentation/menu/parse_util.hpp"      // parse_i, parse_f
#include "presentation/menu/settings_apply.hpp"  // classify_change, ApplyTier, StagedChange
#include "presentation/menu/settings_preview.hpp"  // preview_cheap_key
#include "presentation/menu/settings_seed.hpp"

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

std::optional<MenuModel> load_menu_model() {
    // Built at compile time; cannot fail, cannot be missing, cannot drift from
    // what the build produced.  Kept as optional<> so the call sites — which
    // all handle a missing model — do not have to change shape.
    return built_in_menu_model();
}

SettingsFlow make_pause_flow(MenuModel& model, SettingsSession& session,
                             ConfirmDialog& confirm, PauseFlowDeps* d) {
    SettingsFlow::Hooks h;
    h.persist = [d](const std::string& k, const std::string& v) {
        d->bind->save(k, v);
    };
    h.classify = [d, &session](const std::string& k, const std::string& v) {
        // Set-aware: the Style preset's keys only cross the classic<->HD
        // boundary together (see classify_change_in_set).
        std::vector<std::pair<std::string, std::string>> staged;
        for (const auto& ch : session.changes())
            staged.emplace_back(ch.key, ch.new_value);
        return classify_change_in_set(k, v, d->bind->cur, staged);
    };
    h.apply_begin = [d]() {
        // Seed reinit_req from current rt first; staged reinit-class changes
        // override below.  §8.6 step 4.
        d->reinit_req->enhanced     = d->opts->enhanced;
        d->reinit_req->render_scale = d->opts->render_scale;
        d->reinit_req->hd_profile   = d->opts->hd_profile;
        d->reinit_req->music_device = d->opts->music_device;
        d->reinit_req->sfx_backend  = d->opts->sfx_backend;
    };
    h.apply_change = [d](const StagedChange& ch, ApplyTier tier) {
        // Apply the new value to the live rt / state.
        if (tier == ApplyTier::Reinit) {
            if (ch.key == "enhanced") {
                const bool on = ch.new_value == "true" || ch.new_value == "1";
                d->reinit_req->enhanced = on;
                // smooth-motion is DERIVED from the umbrella, so it has to
                // move with it here too.  Leaving it stale would make the
                // live options disagree with what the reinit is about to
                // build, for however many frames the reinit takes.
                d->opts->enhance.smooth_motion = on;
            }
            else if (ch.key == "render_scale")
                d->reinit_req->render_scale =
                    parse_i(ch.new_value, d->reinit_req->render_scale);
            else if (ch.key == "hd_profile")
                d->reinit_req->hd_profile = ch.new_value;
            else if (ch.key == "music_device")
                d->reinit_req->music_device = ch.new_value;
            else if (ch.key == "sfx_backend")
                d->reinit_req->sfx_backend = ch.new_value;
        }
        // Non-reinit keys: volume/fullscreen already previewed live;
        // hd_profile same-scale → rt.
        if (ch.key == "hd_profile" && tier == ApplyTier::Live &&
            d->bind->rt_hd_profile)
            *d->bind->rt_hd_profile = ch.new_value;
        // enhance.* flags: adopt into the live GameOptions so they take
        // effect in-session (most are read per-frame; the level-entry
        // latches catch up on the next reinit/level).
    };
    h.apply_done = [d](bool needs_reinit) {
        if (needs_reinit) {
            *d->want_reinit = true;
            // pause_open stays true so the pause block sees want_reinit and
            // captures the snapshot.
        }
        // No reinit: just return to pause root.
    };
    // Discard/revert: restore each staged Options preview to its baseline value.
    h.revert_change = [d](const StagedChange& ch) {
        d->bind->mem[ch.key] = ch.old_value;
        // Re-apply the cheap live preview at baseline (shared:
        // settings_preview.hpp); the site-specific live keys follow.
        if (preview_cheap_key(ch.key, ch.old_value, d->bind->audio,
                              d->bind->win, d->bind->enhanced)) {
            // handled
        } else if (ch.key == "hd_profile" && d->bind->rt_hd_profile) {
            *d->bind->rt_hd_profile = ch.old_value;
        } else if (ch.key == "aspect" && d->bind->apply_aspect) {
            d->bind->apply_aspect(ch.old_value);
        }
    };
    h.reopen_options = [d]() { d->menu->open("options"); };
    h.confirm_note = [](bool any_reinit, bool any_persist) {
        if (any_reinit) return std::string("Your game will briefly reload.");
        if (any_persist)
            return std::string("Saved - takes effect on next launch.");
        return std::string{};
    };
    return SettingsFlow(model, session, confirm, std::move(h));
}

MenuActionTable make_pause_actions(PauseActionsDeps* d) {
    return {
        {"resume", [d] { *d->pause_open = false; }},
        {"quit_title", [d] { *d->abort_to_title = true; }},
        {"quit_desktop", [d] { *d->want_quit_program = true; }},
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
            // atoi is safe HERE: menus.json declares cheat.start_level as
            // type "choice" over the closed list [1..7], so this bind only
            // ever holds a digit this engine wrote (see :228). The user cycles
            // the choice, never types into it. If it ever becomes a text field
            // or gets persisted to play.json, this needs a checked parse.
            // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
            const int lvl = v.empty() ? 0 : std::atoi(v.c_str());
            // Leave pause_open set: the run loop's pause block is the only
            // consumer of want_warp — closing the overlay here strands the
            // flag until the menu is next opened.
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
                // Failed quicksave used to be SILENT (read-only dir, full
                // disk) — the user believed they had a checkpoint.
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
    bind.audio = d.audio;
    bind.win = d.sw->win;
    bind.enhanced = d.opts->enhanced;
    bind.persist = &d.opts->persist;
    bind.session = d.session;
    // Tier-classifier wiring: pause → reinit path.
    bind.want_reinit = d.want_reinit;
    bind.reinit_req = d.reinit_req;
    bind.rt_hd_profile = &d.opts->hd_profile;   // opts == run_game's rt
    SettingsSeed seed;
    seed.enhanced = d.opts->enhanced;
    seed.hd_profile = d.opts->hd_profile;
    seed.render_scale = d.opts->render_scale;
    seed.music_device = d.opts->music_device;
    seed.sfx_backend = d.opts->sfx_backend;
    seed.aspect = d.opts->aspect;
    seed.fullscreen = (SDL_GetWindowFlags(d.sw->win) &
                       SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    seed.flags = d.opts->enhance;
    seed_settings_mem(bind, seed);
    // Tier-1 live Aspect: SDL_RenderSetLogicalSize + update run-loop logical_w/h
    // + rt.aspect. No window/audio rebuild, no reload.
    bind.apply_aspect = [opts = d.opts, lsz = d.lsz,
                         hd_scale = d.hd_scale](const std::string& v) {
        opts->aspect = v;
        const LogicalDims ld = aspect_logical(hd_scale, v);
        lsz->set(ld.w, ld.h);   // SDL + mirror together (§3.13)
    };
    bind.mem["cheat.start_level"] = std::to_string(d.display_level);
}

}  // namespace olduvai::presentation
