// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "options_resolve.hpp"

#include <string>

#include "parse_num.hpp"

namespace olduvai::app {

namespace {

// 17 of the 26 keys differ only in WHICH member they write and WHICH guard
// lets them.  Spelling each one out cost 26 near-identical blocks and was how
// the unchecked-atoi family survived so long: nobody reads the 22nd copy.
// Same treatment as fc4862f/87badc1 — one guarded decision per family, and the
// genuinely special keys below stay spelled out because they ARE special.
struct FlagGuarded {          // config fills it unless the CLI flagged it
    const char* key;
    std::string PlaySettings::* field;
    bool PlaySettings::Cli::* flag;
};
constexpr FlagGuarded kFlagGuarded[] = {
    {"autofire",     &PlaySettings::autofire,     &PlaySettings::Cli::autofire},
    {"hd_profile",   &PlaySettings::hd_profile,   &PlaySettings::Cli::hd},
    {"music_device", &PlaySettings::music_device, &PlaySettings::Cli::music_device},
    {"sfx_backend",  &PlaySettings::sfx_backend,  &PlaySettings::Cli::sfx_backend},
    {"display_mode", &PlaySettings::display_mode, &PlaySettings::Cli::display_mode},
    {"transitions",  &PlaySettings::transitions,  &PlaySettings::Cli::transitions},
    {"hd_font",      &PlaySettings::hd_font,      &PlaySettings::Cli::hd_font},
    {"banner_fx",    &PlaySettings::banner_fx,    &PlaySettings::Cli::banner_fx},
    // aspect guards on the FLAG, not the sentinel value: an explicit
    // "--aspect keep" must beat a saved widescreen config (0.9.2 field bug).
    {"aspect",       &PlaySettings::aspect,       &PlaySettings::Cli::aspect},
};

// Gamepad bindings: config-only (no CLI flag exists), so nothing to guard.
struct Unguarded {
    const char* key;
    std::string PlaySettings::* field;
};
constexpr Unguarded kPadBindings[] = {
    {"pad_jump",    &PlaySettings::pad_jump},
    {"pad_attack",  &PlaySettings::pad_attack},
    {"pad_pause",   &PlaySettings::pad_pause},
    {"pad_confirm", &PlaySettings::pad_confirm},
    {"pad_back",    &PlaySettings::pad_back},
};

// Sentinel-guarded: empty means "nobody has said", so config may fill it.
constexpr Unguarded kFillIfEmpty[] = {
    {"mt32_model", &PlaySettings::mt32_model},
    {"rom_dir",    &PlaySettings::rom_dir},
    {"soundfont",  &PlaySettings::soundfont},
};

}  // namespace

void merge_config(PlaySettings& s, const Config& merged) {
    // The one-time Classic/Enhanced question is considered answered once
    // any source (saved config, profile, an explicit CLI flag) states the
    // master flag.
    s.style_answered = merged.count("enhanced") != 0 || s.cli.enhanced;
    if (auto it = merged.find("vga_scan");
        it != merged.end() && !s.cli.vga_scan) {
        s.vga_scan = it->second == "true";
    }
    if (auto it = merged.find("enhanced");
        it != merged.end() && !s.cli.enhanced) {
        s.enhanced = it->second == "true";
    }
    if (auto it = merged.find("enhance");
        it != merged.end() && !s.cli.enhanced && s.enhance_list.empty()) {
        s.enhance_list = it->second;
        s.enhance_list_from_config = true;
    }
    if (auto it = merged.find("render_scale");
        it != merged.end() && !s.cli.scale) {
        parse_int(it->second, s.render_scale);
    }
    if (auto it = merged.find("game_dir");
        it != merged.end() && !s.cli.game_dir) {
        s.game_dir = it->second;
        s.config_game_dir = true;
    }
    // Audio keys (CLI defaults are sentinels; config fills them).
    // Gamepad mapping keys (config-only; SDL button names).
    if (auto it = merged.find("pad_deadzone"); it != merged.end())
        parse_int(it->second, s.pad_deadzone);
    // Tuning keys (sentinel defaults; config fills them when the CLI left
    // the default).  Read-only from config, like the audio keys above —
    // bool flags can't tell "default off" from "explicitly off", so they
    // (vsync/fullscreen) are CLI-only by design.
    // audio_rate / audio_buffer keep the sentinel guard on purpose: 0 means
    // "device default" and is not a value anyone can meaningfully pass, so
    // explicit-equals-sentinel carries the same meaning as unset.  That is the
    // property the six string keys above did NOT have.
    if (auto it = merged.find("audio_rate");
        it != merged.end() && s.audio_rate == 0) {
        parse_int(it->second, s.audio_rate);
    }
    if (auto it = merged.find("audio_buffer");
        it != merged.end() && s.audio_buffer == 0) {
        parse_int(it->second, s.audio_buffer);
    }
    for (const auto& e : kFlagGuarded) {
        if (auto it = merged.find(e.key);
            it != merged.end() && !(s.cli.*e.flag)) {
            s.*e.field = it->second;
        }
    }
    for (const auto& e : kPadBindings) {
        if (auto it = merged.find(e.key); it != merged.end())
            s.*e.field = it->second;
    }
    for (const auto& e : kFillIfEmpty) {
        if (auto it = merged.find(e.key);
            it != merged.end() && (s.*e.field).empty()) {
            s.*e.field = it->second;
        }
    }
    // F5 bug-report destination (config-only; $OLDUVAI_BUG_DIR still
    // overrides).  The caller applies the presentation-side effect.
    if (auto it = merged.find("bug_report_dir");
        it != merged.end() && !it->second.empty()) {
        s.bug_report_dir = it->second;
    }
}

void adopt_preset(PlaySettings& s, const std::string& cli_profile,
                  const std::string& preset) {
    if (preset.empty() || !cli_profile.empty()) return;
    Config pc;
    apply_profile(pc, preset);
    if (auto it = pc.find("enhanced"); it != pc.end() && !s.cli.enhanced)
        s.enhanced = it->second == "true";
    if (auto it = pc.find("enhance"); it != pc.end() && !s.cli.enhanced) {
        s.enhance_list = it->second;
        s.enhance_list_from_config = true;
    }
    if (auto it = pc.find("hd_profile"); it != pc.end() && !s.cli.hd)
        s.hd_profile = it->second;
    if (auto it = pc.find("render_scale"); it != pc.end() && !s.cli.scale)
        parse_int(it->second, s.render_scale);
    if (auto it = pc.find("aspect"); it != pc.end() && !s.cli.aspect)
        s.aspect = it->second;
}

}  // namespace olduvai::app
