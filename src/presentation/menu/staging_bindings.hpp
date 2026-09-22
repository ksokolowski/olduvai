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
#include <vector>

#include <SDL.h>

#include "presentation/audio/audio.hpp"             // SdlAudio
#include "presentation/audio/sound_card.hpp"        // the Sound card row
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
    // The aspect this menu opened with.  DisplaySettings does not carry
    // aspect, and allowed_values needs it: the widescreen gate is re-read on
    // every keypress, so without a memory of what the user ARRIVED with, one
    // step off "widescreen" in classic mode hides the value again and strands
    // it — the very trap this gate exists to avoid.
    std::string aspect_at_entry;
    // Which Sound card choices this machine can play (probe_sound_cards);
    // the row offers only those.  Default: the always-present cards.
    SoundCardAvail sound_avail;

    // Aspect's "widescreen" is offered only when HD is on, because that is
    // literally what the presenter requires: WidescreenPresenter activates on
    // `aspect == "widescreen" && hd() && margin > 0`, so in classic mode the
    // row would offer a choice that does nothing.  Read from the STAGED
    // values, not the entry snapshot, so flipping Style to Enhanced in this
    // same visit makes the value appear.
    //
    // Withholding is safe by construction: Menu::adjust splices a current
    // value the list does not offer back in, so a widescreen user who drops
    // to classic still sees and keeps their setting.
    std::vector<std::string> allowed_values(const MenuItem& it) override {
        // Sound card: only the cards that will sound.  "custom" is never
        // offered — it is how a hand-made pair READS, and Menu::adjust
        // steps from it into this list.
        if (it.key == "sound_card") return available_sound_cards(sound_avail);
        if (it.key != "aspect") return it.values;
        const auto e = mem.find("enhanced");
        const auto p = mem.find("hd_profile");
        const bool on = hd_active(
            e == mem.end() ? cur.enhanced : e->second == "true",
            p == mem.end() ? cur.hd_profile : p->second);
        // ...OR when it is what the user currently HOLDS.  Withholding a
        // value someone already has would make it unreachable the moment they
        // cycled off it — the same trap in a new place — so the gate hides
        // widescreen only from people who do not have it.
        const auto a = mem.find("aspect");
        if (on || aspect_at_entry == "widescreen" ||
            (a != mem.end() && a->second == "widescreen"))
            return it.values;
        std::vector<std::string> out;
        for (const auto& v : it.values)
            if (v != "widescreen") out.push_back(v);
        return out;
    }

    std::string get(const std::string& k) override final {
        std::string special;
        if (get_special(k, special)) return special;
        // Sound card is a VIEW of the two audio keys, never stored itself.
        if (k == "sound_card")
            return sound_card_for(get("music_device"), get("sfx_backend"));
        const auto it = mem.find(k);
        return it == mem.end() ? std::string{} : it->second;
    }
    void save(const std::string& key, const std::string& v) {
        if (persist && *persist) (*persist)(key, v);
    }
    void set(const std::string& k, const std::string& v) override final {
        if (set_special(k, v)) return;
        if (k == "sound_card") {
            // Fan the card out to the pair it names, through this same set(),
            // so each key stages (and classifies, and persists) as it always
            // has.  "custom" and unknown names name no pair: nothing to do.
            if (const SoundCard* c = find_sound_card(v)) {
                set("music_device", c->music);
                set("sfx_backend", c->sfx);
            }
            return;
        }
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
