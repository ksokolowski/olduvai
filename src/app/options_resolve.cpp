// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "options_resolve.hpp"

#include <string>
#include <variant>

#include "parse_num.hpp"
#include "presentation/input/button_layout.hpp"
#include "presentation/menu/profile_table.hpp"

namespace olduvai::app {

namespace {

using PS = PlaySettings;

// What may stop a play.json key from filling its field.
enum class Guard {
    kCli,            // the command line stated it (its cli.* flag)
    kCliOrFilled,    // that, or the field already holds a list
    kFilled,         // the field is already non-empty (someone said)
    kSet,            // the field is non-zero (0 = device default = unset)
    kNone,           // config-only: nothing else sets it
    kEmptyValue,     // config-only, and an empty value means "unset"
};

// One play.json key: the PlaySettings field it fills and its guard.  The
// config merge, a profile pin and --save-config all read this table.
struct Key {
    const char* name;
    std::variant<std::string PS::*, int PS::*, bool PS::*> field;
    Guard guard;
    bool PS::Cli::* cli;       // the flag that marks it CLI-stated, or null
    bool PS::* from_config;    // raised when the config filled it, or null
};

const Key kKeys[] = {
    {"vga_scan",     &PS::vga_scan,     Guard::kCli, &PS::Cli::vga_scan, nullptr},
    {"enhanced",     &PS::enhanced,     Guard::kCli, &PS::Cli::enhanced, nullptr},
    // The granular list is a subset the flag states as a whole.
    {"enhance",      &PS::enhance_list, Guard::kCliOrFilled,
                     &PS::Cli::enhanced, &PS::enhance_list_from_config},
    {"render_scale", &PS::render_scale, Guard::kCli, &PS::Cli::scale, nullptr},
    {"game_dir",     &PS::game_dir,     Guard::kCli, &PS::Cli::game_dir,
                     &PS::config_game_dir},
    {"autofire",     &PS::autofire,     Guard::kCli, &PS::Cli::autofire, nullptr},
    {"hd_profile",   &PS::hd_profile,   Guard::kCli, &PS::Cli::hd, nullptr},
    {"music_device", &PS::music_device, Guard::kCli, &PS::Cli::music_device, nullptr},
    {"sfx_backend",  &PS::sfx_backend,  Guard::kCli, &PS::Cli::sfx_backend, nullptr},
    {"display_mode", &PS::display_mode, Guard::kCli, &PS::Cli::display_mode, nullptr},
    {"transitions",  &PS::transitions,  Guard::kCli, &PS::Cli::transitions, nullptr},
    {"hd_font",      &PS::hd_font,      Guard::kCli, &PS::Cli::hd_font, nullptr},
    {"banner_fx",    &PS::banner_fx,    Guard::kCli, &PS::Cli::banner_fx, nullptr},
    // Guards on the FLAG, not the value: an explicit "--aspect keep" must
    // beat a saved widescreen config (0.9.2 field bug).
    {"aspect",       &PS::aspect,       Guard::kCli, &PS::Cli::aspect, nullptr},
    // Empty means nobody has said; the flag and the config both fill it.
    {"mt32_model",   &PS::mt32_model,   Guard::kFilled, nullptr, nullptr},
    {"rom_dir",      &PS::rom_dir,      Guard::kFilled, nullptr, nullptr},
    {"soundfont",    &PS::soundfont,    Guard::kFilled, nullptr, nullptr},
    // 0 is "device default", not a value anyone passes, so an explicit 0
    // means the same as unset.
    {"audio_rate",   &PS::audio_rate,   Guard::kSet, nullptr, nullptr},
    {"audio_buffer", &PS::audio_buffer, Guard::kSet, nullptr, nullptr},
    // Config-only: no flag, no menu row.  The smooth-present text is
    // validated in build_game_options (warn + auto, never fatal).
    {"pad_deadzone", &PS::pad_deadzone, Guard::kNone, nullptr, nullptr},
    {"pad_jump",     &PS::pad_jump,     Guard::kNone, nullptr, nullptr},
    {"pad_attack",   &PS::pad_attack,   Guard::kNone, nullptr, nullptr},
    {"pad_pause",    &PS::pad_pause,    Guard::kNone, nullptr, nullptr},
    {"pad_confirm",  &PS::pad_confirm,  Guard::kNone, nullptr, nullptr},
    {"pad_back",     &PS::pad_back,     Guard::kNone, nullptr, nullptr},
    {"smooth_subframes", &PS::smooth_subframes, Guard::kNone, nullptr, nullptr},
    {"smooth_vsync", &PS::smooth_vsync, Guard::kNone, nullptr, nullptr},
    // F5 destination ($OLDUVAI_BUG_DIR still overrides; the caller applies
    // it).
    {"bug_report_dir", &PS::bug_report_dir, Guard::kEmptyValue, nullptr, nullptr},
};

const Key* find_key(const std::string& name) {
    for (const auto& k : kKeys)
        if (name == k.name) return &k;
    return nullptr;
}

bool cli_stated(const PS& s, const Key& k) {
    return k.cli != nullptr && s.cli.*k.cli;
}

// Whether the config layer may fill `k` (a profile pin asks only
// cli_stated: it beats the config it sits on).
bool config_may_fill(const PS& s, const Key& k, const std::string& value) {
    switch (k.guard) {
        case Guard::kCli:         return !cli_stated(s, k);
        case Guard::kCliOrFilled:
            return !cli_stated(s, k) &&
                   (s.*std::get<std::string PS::*>(k.field)).empty();
        case Guard::kFilled:
            return (s.*std::get<std::string PS::*>(k.field)).empty();
        case Guard::kSet:         return s.*std::get<int PS::*>(k.field) == 0;
        case Guard::kNone:        return true;
        case Guard::kEmptyValue:  return !value.empty();
    }
    return false;
}

void assign(PS& s, const Key& k, const std::string& value) {
    if (const auto* f = std::get_if<std::string PS::*>(&k.field)) {
        s.**f = value;
    } else if (const auto* i = std::get_if<int PS::*>(&k.field)) {
        parse_int(value, s.**i);   // a malformed value leaves the default
    } else {
        s.*std::get<bool PS::*>(k.field) = value == "true";
    }
    if (k.from_config != nullptr) s.*k.from_config = true;
}

std::string text_of(const PS& s, const Key& k) {
    if (const auto* f = std::get_if<std::string PS::*>(&k.field)) return s.**f;
    if (const auto* i = std::get_if<int PS::*>(&k.field))
        return std::to_string(s.**i);
    return s.*std::get<bool PS::*>(k.field) ? "true" : "false";
}

}  // namespace

void merge_config(PlaySettings& s, const Config& merged) {
    // The one-time Classic/Enhanced question is considered answered once
    // any source (saved config, profile, an explicit CLI flag) states the
    // master flag.
    s.style_answered = merged.count("enhanced") != 0 || s.cli.enhanced;
    for (const auto& k : kKeys) {
        const auto it = merged.find(k.name);
        if (it != merged.end() && config_may_fill(s, k, it->second))
            assign(s, k, it->second);
    }
}

bool adopt_profile_key(PlaySettings& s, const std::string& key,
                       const std::string& value) {
    const Key* k = find_key(key);
    if (k == nullptr) return false;
    if (!cli_stated(s, *k)) assign(s, *k, value);
    return true;
}

void adopt_preset(PlaySettings& s, const std::string& cli_profile,
                  const std::string& preset) {
    if (preset.empty() || !cli_profile.empty()) return;
    Config pc;
    apply_profile(pc, preset);
    for (const auto& [k, v] : pc) adopt_profile_key(s, k, v);
}

namespace {

// The button layout a family's devices print, as pad_* keys in `cfg`.
void apply_device_layout(Config& cfg, const std::string& family) {
    const char* id = presentation::family_button_layout(family);
    const auto* layout =
        id != nullptr ? presentation::find_button_layout(id) : nullptr;
    if (layout == nullptr) return;
    presentation::PadBindings b = presentation::bindings_of(*layout);
    for (const char* key : presentation::kPadKeys)
        cfg[key] = *presentation::binding_for_key(b, key);
}

}  // namespace

LayeredConfig layer_config(const Config& file_cfg,
                           const std::string& cli_profile,
                           const std::string& default_profile) {
    LayeredConfig out;
    out.family = presentation::kDefaultFamily;
    if (!default_profile.empty()) {
        if (const auto* p = presentation::find_profile(default_profile)) {
            apply_profile(out.merged, default_profile);
            out.family = p->family;
            apply_device_layout(out.merged, p->family);
        } else {
            out.warnings.push_back(
                "olduvai: unknown --default-profile '" + default_profile +
                "' — using the desktop defaults (known: " +
                presentation::profile_names() + ")\n");
        }
    }
    for (const auto& [k, v] : file_cfg) out.merged[k] = v;
    if (!cli_profile.empty()) {
        apply_profile(out.merged, cli_profile);
        if (const auto* p = presentation::find_profile(cli_profile))
            out.family = p->family;
    }
    return out;
}

Config config_to_save(const Config& file_cfg, const std::string& cli_profile,
                      const PlaySettings& ps, const std::string& game_dir) {
    Config out = file_cfg;
    if (!cli_profile.empty()) apply_profile(out, cli_profile);
    for (const auto& k : kKeys) {
        if (!cli_stated(ps, k)) continue;
        // The directory as resolved for this run, not the flag's text.
        const std::string v = k.name == std::string("game_dir") ? game_dir
                                                           : text_of(ps, k);
        // An empty granular list states nothing.
        if (v.empty() && k.guard == Guard::kCliOrFilled) continue;
        out[k.name] = v;
    }
    return out;
}

}  // namespace olduvai::app
