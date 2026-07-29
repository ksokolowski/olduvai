// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Shared staging skeleton for the three settings-staging MenuBindings — the
// in-game Pause menu, the main-menu Options batch, and the boss Pause menu.
// All three hand-rolled a byte-identical get/set/save skeleton (preset fan-out,
// cheat.* session-only skip, old_val capture, cheap-key live preview, and
// session->stage()), diverging only at a few real variation points.  This
// concrete intermediate owns the invariant skeleton (final get/set/save) and
// exposes those points as protected hooks (audit A3).  Scope is the three
// settings-staging sites only; report_form::Bind (a mem-only form store, no
// preset/cheat/preview/session path) deliberately stays a plain MenuBindings.
#pragma once

#include <functional>
#include <map>
#include <string>

#include <SDL.h>

#include "presentation/audio/audio.hpp"             // SdlAudio
#include "presentation/menu/menu.hpp"              // MenuBindings
#include "presentation/menu/settings_apply.hpp"    // ApplyTier, classify_change,
                                              // DisplaySettings, apply_preset
#include "presentation/menu/settings_preview.hpp"  // preview_cheap_key
#include "presentation/menu/settings_session.hpp"  // SettingsSession

namespace olduvai::presentation {

using PersistFn = std::function<void(const std::string&, const std::string&)>;

// The invariant staging skeleton shared by every settings-staging menu.
// Derived classes set the public wiring fields and, where they genuinely
// differ, override the protected hooks:
//   get_special        — keys read from live state, not `mem` (Pause: cheat.god,
//                        autofire).  Return true + fill `out` to short-circuit.
//   set_special        — keys applied live + returned BEFORE staging (Pause:
//                        cheat.god, autofire).  Return true when handled.
//   apply_live_preview — the non-cheap live path taken after preview_cheap_key()
//                        declines (Pause/Title: same-scale hd_profile swap +
//                        Tier-1 aspect).  Default no-op (Boss: staged only).
struct StagingBindings : MenuBindings {
    SdlAudio* audio = nullptr;
    SDL_Window* win = nullptr;
    bool enhanced = false;
    const PersistFn* persist = nullptr;    // → play.json (app layer)
    std::map<std::string, std::string> mem;
    SettingsSession* session = nullptr;    // batched staging (set post-ctor)
    DisplaySettings cur;                   // rt snapshot at menu entry

    std::string get(const std::string& k) override final {
        std::string special;
        if (get_special(k, special)) return special;
        const auto it = mem.find(k);
        return it == mem.end() ? std::string{} : it->second;
    }
    void save(const std::string& key, const std::string& v) {
        if (persist && *persist) (*persist)(key, v);
    }
    void set(const std::string& k, const std::string& v) override final {
        if (set_special(k, v)) return;
        if (k == "preset") {
            // One-click Classic/HD preset: fan the bundle out through this
            // same set() so every key rides the normal machinery.
            mem[k] = v;
            apply_preset(*this, v);
            return;
        }
        // cheat.* keys are session-only: no staging, no persist.  (Boss never
        // reaches the cheats submenu, so inheriting this skip is inert there.)
        if (k.rfind("cheat.", 0) == 0) {
            mem[k] = v;
            return;
        }
        // All editable settings keys — enhance.* included — stage the change
        // provisionally; play.json sees nothing until Apply.  Cheap keys
        // preview live for immediate feedback; heavy ones are staged only.
        const std::string old_val = mem.count(k) ? mem[k] : std::string{};
        mem[k] = v;
        if (!preview_cheap_key(k, v, audio, win, enhanced)) {
            apply_live_preview(k, v);
        }
        if (session) session->stage(k, k, old_val, v);
    }

  protected:
    virtual bool get_special(const std::string& /*k*/, std::string& /*out*/) {
        return false;
    }
    virtual bool set_special(const std::string& /*k*/, const std::string& /*v*/) {
        return false;
    }
    virtual void apply_live_preview(const std::string& /*k*/,
                                    const std::string& /*v*/) {}
};

}  // namespace olduvai::presentation
