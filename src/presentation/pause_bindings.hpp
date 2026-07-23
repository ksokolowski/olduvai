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

namespace olduvai::presentation {

using PersistFn = std::function<void(const std::string&, const std::string&)>;

    struct PauseBindings : MenuBindings {
        bool* god = nullptr;
        std::string* autofire = nullptr;   // → GameOptions::autofire token
        SdlAudio* audio = nullptr;
        SDL_Window* win = nullptr;
        bool enhanced = false;
        const PersistFn* persist = nullptr;   // → play.json (app layer)
        std::map<std::string, std::string> mem;
        // Tier-classifier wiring: signal reinit back to run_game.
        bool* want_reinit = nullptr;
        PendingReinit* reinit_req = nullptr;
        DisplaySettings cur;               // snapshot of rt at level entry
        std::string* rt_hd_profile = nullptr;
        // Tier-1 live Aspect: applies SDL_RenderSetLogicalSize + updates the
        // run-loop's logical_w/h + rt.aspect.  Set from the menu loop.
        std::function<void(const std::string&)> apply_aspect;
        // Batched staging session: every editable key change goes here instead
        // of being persisted immediately.  Pointer is set to the local
        // SettingsSession after construction.  §8.6.
        SettingsSession* session = nullptr;
        std::string get(const std::string& k) override {
            if (k == "cheat.god") return (god && *god) ? "1" : "0";
            if (k == "autofire") return autofire ? *autofire : "off";
            auto it = mem.find(k);
            return it == mem.end() ? std::string{} : it->second;
        }
        void save(const std::string& key, const std::string& v) {
            if (persist && *persist) (*persist)(key, v);
        }
        void set(const std::string& k, const std::string& v) override {
            if (k == "cheat.god") { if (god) *god = (v == "1"); return; }
            if (k == "autofire") {   // live apply + persist, never staged
                if (autofire) *autofire = v;
                save("autofire", v);
                return;
            }
            if (k == "preset") {
                // One-click Classic/HD preset: fan the bundle out through
                // this same set() so every key rides the normal machinery.
                mem[k] = v;
                apply_preset(*this, v);
                return;
            }

            // cheat.* keys are session-only: no staging, no persist.
            if (k.rfind("cheat.", 0) == 0) {
                mem[k] = v;
                return;
            }
            // All editable settings keys — enhance.* included — stage the
            // change provisionally; play.json sees nothing until Apply
            // (encode_enhance_persist translates the flags into the single
            // "enhance" config list there).
            // Safe/cheap ones preview live so the user gets immediate feedback
            // (volume, fullscreen, same-scale hd_profile); heavy ones are
            // staged only — no reinit until the user confirms.  §8.6.
            const std::string old_val = mem.count(k) ? mem[k] : std::string{};
            mem[k] = v;
            // Live preview for cheap keys (shared: settings_preview.hpp).
            if (preview_cheap_key(k, v, audio, win, enhanced)) {
                // handled — still stages below
            } else if (k == "hd_profile") {
                const ApplyTier tier = classify_change(k, v, cur);
                if (tier == ApplyTier::Live && rt_hd_profile)
                    *rt_hd_profile = v;   // same-scale: live swap
            } else if (k == "aspect" && apply_aspect) {
                apply_aspect(v);          // Tier-1 live: logical-size only
            }
            // Stage for the confirm dialog (no persist, no reinit yet).
            if (session) session->stage(k, k, old_val, v);
        }
};

}  // namespace olduvai::presentation
