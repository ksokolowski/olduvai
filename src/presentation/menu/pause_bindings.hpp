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

#include "presentation/audio/audio.hpp"           // SdlAudio
#include "presentation/menu/menu.hpp"            // MenuBindings
#include "presentation/menu/parse_util.hpp"      // parse_f
#include "presentation/menu/settings_apply.hpp"  // ApplyTier, classify_change, DisplaySettings
#include "presentation/menu/settings_preview.hpp"  // preview_cheap_key
#include "presentation/menu/settings_session.hpp"  // SettingsSession
#include "presentation/menu/staging_bindings.hpp"  // StagingBindings, PersistFn

namespace olduvai::presentation {

// In-game Pause: adds the live cheat.god / autofire keys (read + applied
// straight through to game state, never staged).  Everything else is the
// shared skeleton.
struct PauseBindings : StagingBindings {
    // LIVE target: run_platform_level's per-LEVEL `god_active` local, so a
    // toggle takes effect immediately in the running level.
    bool* god = nullptr;
    // SESSION target: GameOptions::god, which every level (and the boss lives
    // seed) re-derives god_active from; without it the cheat ends at the level.
    bool* god_session = nullptr;
    std::string* autofire = nullptr;   // → GameOptions::autofire token

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
};

}  // namespace olduvai::presentation
