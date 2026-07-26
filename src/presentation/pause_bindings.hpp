// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// In-game Pause menu settings bindings — the MenuBindings impl that backs the
// pause Options subtree (get/set staging, live-preview of cheap keys, reinit
// signalling). Extracted verbatim from game_app.cpp (CC2c). The instance +
// its dep wiring (pause_bind.god = &god_active, …) stay in run_platform_level.

#pragma once

#include <functional>
#include <map>
#include <string>

#include <SDL.h>

#include "presentation/audio.hpp"           // SdlAudio
#include "presentation/menu.hpp"            // MenuBindings
#include "presentation/parse_util.hpp"      // parse_f
#include "presentation/settings_apply.hpp"  // ApplyTier, classify_change, DisplaySettings
#include "presentation/settings_preview.hpp"  // preview_cheap_key
#include "presentation/settings_session.hpp"  // SettingsSession
#include "presentation/staging_bindings.hpp"  // StagingBindings, PersistFn

namespace olduvai::presentation {

// In-game Pause: adds the live cheat.god / autofire keys (read + applied
// straight through to game state, never staged) and the Tier-1 live path for
// same-scale hd_profile + aspect.  Everything else is the shared skeleton.
struct PauseBindings : StagingBindings {
    // LIVE target: run_platform_level's per-LEVEL `god_active` local, so a
    // toggle takes effect immediately in the running level.
    bool* god = nullptr;
    // SESSION target: GameOptions::god, the flag every level entry re-derives
    // `god_active` from.  Without it a god toggle made in the pause menu died
    // at the next level boundary — the next level recomputed god_active from
    // GameOptions::god and silently reverted to whatever --god said.  Writing
    // both makes the cheat live AND sticky across L1→L7 (and into the boss
    // arenas, whose lives seed reads GameOptions::god).
    bool* god_session = nullptr;
    std::string* autofire = nullptr;   // → GameOptions::autofire token
    // Tier-classifier wiring: signal reinit back to run_game.
    bool* want_reinit = nullptr;
    PendingReinit* reinit_req = nullptr;
    std::string* rt_hd_profile = nullptr;
    // Tier-1 live Aspect: applies SDL_RenderSetLogicalSize + updates the
    // run-loop's logical_w/h + rt.aspect.  Set from the menu loop.
    std::function<void(const std::string&)> apply_aspect;

  protected:
    bool get_special(const std::string& k, std::string& out) override {
        if (k == "cheat.god") { out = (god && *god) ? "1" : "0"; return true; }
        if (k == "autofire") { out = autofire ? *autofire : "off"; return true; }
        return false;
    }
    bool set_special(const std::string& k, const std::string& v) override {
        if (k == "cheat.god") {
            const bool on = (v == "1");
            if (god) *god = on;                   // live, this level
            if (god_session) *god_session = on;   // sticky, across levels
            return true;
        }
        if (k == "autofire") {   // live apply + persist, never staged
            if (autofire) *autofire = v;
            save("autofire", v);
            return true;
        }
        return false;
    }
    void apply_live_preview(const std::string& k,
                            const std::string& v) override {
        if (k == "hd_profile") {
            const ApplyTier tier = classify_change(k, v, cur);
            if (tier == ApplyTier::Live && rt_hd_profile)
                *rt_hd_profile = v;   // same-scale: live swap
        } else if (k == "aspect" && apply_aspect) {
            apply_aspect(v);          // Tier-1 live: logical-size only
        }
    }
};

}  // namespace olduvai::presentation
