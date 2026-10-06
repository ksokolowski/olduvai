// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Shared staging skeleton for the settings MenuBindings of the in-game Pause,
// the main-menu Options and the boss Pause.  report_form::Bind (a plain form
// store) is not one.
#pragma once

#include <cstdlib>
#include <functional>
#include <map>
#include <utility>
#include <string>
#include <vector>

#include <SDL.h>

#include "enhance/upscale.hpp"                    // describe_hd_scaler
#include "presentation/audio/audio.hpp"             // SdlAudio
#include "presentation/audio/sound_card.hpp"        // the Sound card row
#include "presentation/input/actions.hpp"           // the keyboard mapping
#include "presentation/input/button_layout.hpp"     // the Button layout row
#include "presentation/input/gamepad.hpp"           // the live pad mapping
#include "presentation/menu/menu.hpp"              // MenuBindings
#include "presentation/menu/profile_table.hpp"     // family_button_layout
#include "presentation/menu/settings_apply.hpp"    // ApplyTier, classify_change,
                                              // DisplaySettings, apply_preset
#include "presentation/menu/settings_flow.hpp"     // SettingsFlow::Hooks
#include "presentation/menu/settings_preview.hpp"  // preview_cheap_key
#include "presentation/menu/settings_seed.hpp"     // SettingsSeed
#include "presentation/menu/settings_session.hpp"  // SettingsSession
#include "presentation/window_util.hpp"            // window_fullscreen

namespace olduvai::presentation {

using PersistFn = std::function<void(const std::string&, const std::string&)>;

// The staging skeleton: preset fan-out, the session-only cheat.* skip,
// old-value capture, live preview, session staging.  Hooks for the real
// differences:
//   get_special  keys read from live state (Pause: cheat.god, autofire);
//                true + `out` short-circuits.
//   set_special  keys applied live before staging (Pause); true = done.
struct StagingBindings : MenuBindings {
    SdlAudio* audio = nullptr;
    SDL_Window* win = nullptr;
    bool enhanced = false;
    const PersistFn* persist = nullptr;    // → play.json (app layer)
    std::map<std::string, std::string> mem;
    SettingsSession* session = nullptr;    // batched staging (set post-ctor)
    DisplaySettings cur;                   // rt snapshot at menu entry
    // The aspect at menu entry, so allowed_values keeps offering "widescreen"
    // to someone who arrived with it (DisplaySettings has no aspect).
    std::string aspect_at_entry;
    // Which Sound card choices this machine can play (probe_sound_cards);
    // the row offers only those.  Default: the always-present cards.
    SoundCardAvail sound_avail;
    // The layout "Reset" returns the pad to: the device family's.
    std::string default_layout = "xbox";
    // Live preview beyond the cheap keys; null = staged only (boss).
    std::string* live_hd_profile = nullptr;   // same-scale hd_profile swap
    std::function<void(const std::string&)> apply_aspect;   // logical size only

    // The environment the menu previews into, and the values it opens onto:
    // the options `o` (their settings, persist hook, and the Sound cards
    // their ROMs and SoundFont allow).  A template, as settings_seed_of.
    template <class Opts>
    void attach(SdlAudio* a, SDL_Window* w, SettingsSession* s,
                const Opts& o) {
        audio = a;
        win = w;
        persist = &o.persist;
        session = s;
        sound_avail = probe_sound_cards(o.rom_dir, o.soundfont);
        const SettingsSeed seed =
            settings_seed_of(o, o.enhance, window_fullscreen(w));
        enhanced = seed.enhanced;
        seed_settings_mem(*this, seed);
        // The pad and keyboard mappings as they are now (a previous Apply
        // may have moved them since launch).
        for (const PadSlot& s : kPadSlots) mem[s.key] = gamepad::binding(s.key);
        for (const KeyAction& k : kKeyActions) mem[k.key] = key_binding(k.key);
        if (const char* id = family_button_layout(o.profile_family))
            default_layout = id;
    }

    // After an adopt replaced the pipeline: its audio and window, and the
    // applied settings as the baseline.  `mem` already holds the applied
    // values; the live ones (volumes) are not re-seeded.
    void rebind(SdlAudio* a, SDL_Window* w, const DisplaySettings& applied) {
        audio = a;
        win = w;
        enhanced = applied.enhanced;
        cur = applied;
        if (cur.hd_profile.empty()) cur.hd_profile = "native";
    }

    // "widescreen" is offered only with HD on (WidescreenPresenter needs hd()
    // and a margin), read from the staged values so switching Style to Enhanced
    // in the same visit shows it.  Menu::adjust splices a held value back in,
    // so a widescreen user dropping to classic keeps it.
    std::vector<std::string> allowed_values(const MenuItem& it) override {
        // Sound card: only the cards that will sound.  "custom" is never
        // offered; it is how a hand-made pair reads.
        if (it.key == "sound_card") return available_sound_cards(sound_avail);
        // Likewise "custom" is how a hand-made mapping reads, not a pick.
        if (it.key == "button_layout") {
            std::vector<std::string> ids;
            for (const ButtonLayout& l : kButtonLayouts) ids.emplace_back(l.id);
            return ids;
        }
        if (it.key != "aspect") return it.values;
        const auto e = mem.find("enhanced");
        const auto p = mem.find("hd_profile");
        const bool on = hd_active(
            e == mem.end() ? cur.enhanced : e->second == "true",
            p == mem.end() ? cur.hd_profile : p->second);
        // ...or when the user holds it, so it never becomes unreachable.
        const auto a = mem.find("aspect");
        if (on || aspect_at_entry == "widescreen" ||
            (a != mem.end() && a->second == "widescreen"))
            return it.values;
        std::vector<std::string> out;
        for (const auto& v : it.values)
            if (v != "widescreen") out.push_back(v);
        return out;
    }

    // The Upscaler's "Runs" row: what the staged profile and scale will
    // actually execute, a view of three keys and never stored.
    std::string hd_runs_text() const {
        const auto at = [&](const char* k, const std::string& dflt) {
            const auto i = mem.find(k);
            return i == mem.end() ? dflt : i->second;
        };
        const std::string profile = at("hd_profile", cur.hd_profile);
        const bool on = at("enhanced", cur.enhanced ? "true" : "false") == "true";
        const int rs = std::atoi(at("render_scale", std::to_string(cur.render_scale)).c_str());
        return enhance::describe_hd_scaler(profile, hd_scale_for(on, profile, rs));
    }

    std::string get(const std::string& k) final {
        std::string special;
        if (get_special(k, special)) return special;
        // Sound card is a VIEW of the two audio keys, never stored itself.
        if (k == "sound_card")
            return sound_card_for(get("music_device"), get("sfx_backend"));
        if (k == "button_layout") return button_layout_for(pad_bindings());
        if (k == "hd_runs") return hd_runs_text();
        const auto it = mem.find(k);
        return it == mem.end() ? std::string{} : it->second;
    }
    void save(const std::string& key, const std::string& v) const {
        if (persist && *persist) (*persist)(key, v);
    }
    void set(const std::string& k, const std::string& v) final {
        if (set_special(k, v)) return;
        if (k == "sound_card") {
            // Fan the card out to its pair through set(), so each key stages,
            // classifies and persists as usual.  "custom" and unknown names do
            // nothing.
            if (const SoundCard* c = find_sound_card(v)) {
                set("music_device", c->music);
                set("sfx_backend", c->sfx);
            }
            return;
        }
        // Button layout and the rebindable actions: compute the whole new
        // mapping (a layout's five keys; a rebind and the action it swapped
        // with), then stage each key that moved.
        PadBindings pad = pad_bindings();
        bool pad_key = false;
        if (k == "button_layout") {
            const ButtonLayout* l = find_button_layout(v);
            if (l == nullptr) return;   // "custom" and unknown ids: no-op
            pad = bindings_of(*l);
            pad_key = true;
        } else {
            pad_key = rebind_action(pad, k, v);
        }
        if (pad_key) {
            stage_pad(pad);
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
        stage(k, v);
    }

    // A binding row's slot, by the rules of its device: the pad's contexts
    // (rebind_action), or the keyboard's play actions as one group.
    bool set_binding(const std::string& key, int slot,
                     const std::string& name) override {
        if (key.rfind("pad_", 0) == 0) {
            PadBindings pad = pad_bindings();
            if (!rebind_action(pad, key, name, slot)) return false;
            stage_pad(pad);
            return true;
        }
        std::vector<BindingList> lists;
        std::vector<BindingList*> group;
        std::size_t target = std::size(kKeyActions);
        for (const KeyAction& k : kKeyActions) {
            if (key == k.key) target = lists.size();
            lists.push_back(split_binding(get(k.key)));
        }
        if (target == std::size(kKeyActions))
            return MenuBindings::set_binding(key, slot, name);
        for (BindingList& l : lists) group.push_back(&l);
        if (!rebind_in_group(group, target, slot, name)) return false;
        for (std::size_t i = 0; i < lists.size(); ++i) {
            const std::string v = join_binding(lists[i]);
            if (v != get(kKeyActions[i].key)) stage(kKeyActions[i].key, v);
        }
        return true;
    }

    void clear_alternate(const std::string& key) override {
        PadBindings pad = pad_bindings();
        if (presentation::clear_alternate(pad, key))
            stage_pad(pad);
        else if (key.rfind("pad_", 0) != 0)
            MenuBindings::clear_alternate(key);
    }

    // The Reset rows: the pad back to the device's layout, the keyboard to
    // its defaults, or both.
    bool run_action(const std::string& action) override {
        const bool all = action == "reset_controls";
        if (!all && action != "reset_pad" && action != "reset_keys")
            return false;
        if (all || action == "reset_pad")
            if (const ButtonLayout* l = find_button_layout(default_layout))
                stage_pad(bindings_of(*l));
        if (all || action == "reset_keys")
            for (const KeyAction& k : kKeyActions) {
                const std::string v = default_key_setting(k.action);
                if (v != get(k.key)) stage(k.key, v);
            }
        return true;
    }

    // Discard: the change's value and live preview back to baseline.
    void revert(const StagedChange& ch) {
        mem[ch.key] = ch.old_value;
        if (preview_cheap_key(ch.key, ch.old_value, audio, win, enhanced)) return;
        if (ch.key == "hd_profile" && live_hd_profile)
            *live_hd_profile = ch.old_value;
        else if (ch.key == "aspect" && apply_aspect)
            apply_aspect(ch.old_value);
    }

  protected:
    virtual bool get_special(const std::string& /*k*/, std::string& /*out*/) {
        return false;
    }
    virtual bool set_special(const std::string& /*k*/, const std::string& /*v*/) {
        return false;
    }

  private:
    // The staged pad mapping, from mem.
    PadBindings pad_bindings() const {
        PadBindings out;
        for (const PadSlot& s : kPadSlots) {
            const auto it = mem.find(s.key);
            if (it != mem.end()) out.*s.field = it->second;
        }
        return out;
    }

    // Stage each pad_* key `pad` moves.
    void stage_pad(const PadBindings& pad) {
        const PadBindings now = pad_bindings();
        for (const PadSlot& s : kPadSlots)
            if (pad.*s.field != now.*s.field) stage(s.key, pad.*s.field);
    }

    // Stage one key: play.json sees nothing until Apply.  Cheap keys preview
    // live; the pad mapping does not (it would move Confirm and Back under
    // the player mid-menu) and applies with the Apply.
    void stage(const std::string& k, const std::string& v) {
        const std::string old_val = mem.count(k) ? mem[k] : std::string{};
        mem[k] = v;
        if (!preview_cheap_key(k, v, audio, win, enhanced)) {
            if (k == "hd_profile") {
                if (live_hd_profile &&
                    classify_change(k, v, cur) == ApplyTier::Live)
                    *live_hd_profile = v;
            } else if (k == "aspect" && apply_aspect) {
                apply_aspect(v);
            }
        }
        if (session) session->stage(k, k, old_val, v);
    }
};

// The SettingsFlow hooks every Options menu shares: persist, set-aware
// classify (the Style preset's keys cross classic<->HD together), revert,
// reopen, value_of.  Sites add apply_* and confirm_note.
// A staged change's tier against the baseline, the whole staged set in view
// (a Style preset and a scale in one Apply combine).
inline ApplyTier classify_staged(const StagingBindings& b,
                                 const SettingsSession& session,
                                 const std::string& k, const std::string& v) {
    std::vector<std::pair<std::string, std::string>> staged;
    for (const auto& ch : session.changes())
        staged.emplace_back(ch.key, ch.new_value);
    return classify_change_in_set(k, v, b.cur, staged);
}

inline SettingsFlow::Hooks staging_flow_hooks(StagingBindings& b,
                                              SettingsSession& session,
                                              Menu* menu) {
    SettingsFlow::Hooks h;
    h.persist = [&b](const std::string& k, const std::string& v) {
        b.save(k, v);
        gamepad::apply_binding(k, v);   // a pad_* key: from now on
        apply_key_binding(k, v);        // a key_* key
    };
    h.classify = [&b, &session](const std::string& k, const std::string& v) {
        return classify_staged(b, session, k, v);
    };
    h.revert_change = [&b](const StagedChange& ch) { b.revert(ch); };
    h.reopen_options = [menu] { menu->open("options"); };
    h.value_of = [&b](const std::string& k) { return b.get(k); };
    return h;
}

}  // namespace olduvai::presentation
