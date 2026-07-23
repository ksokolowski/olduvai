// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "options_resolve.hpp"

#include <cstdlib>

namespace olduvai::app {

void merge_config(PlaySettings& s, const Config& merged) {
    // The one-time Classic/Enhanced question is considered answered once
    // any source (saved config, profile, an explicit CLI flag) states the
    // master flag.
    s.style_answered = merged.count("enhanced") != 0 || s.cli.enhanced;
    if (auto it = merged.find("vga_scan");
        it != merged.end() && !s.cli.vga_scan) {
        s.vga_scan = it->second == "true";
    }
    if (auto it = merged.find("autofire");
        it != merged.end() && !s.cli.autofire) {
        s.autofire = it->second;
    }
    if (auto it = merged.find("enhanced");
        it != merged.end() && !s.cli.enhanced) {
        s.enhanced = it->second == "true";
    }
    if (auto it = merged.find("enhance");
        it != merged.end() && !s.cli.enhanced && s.enhance_list.empty()) {
        s.enhance_list = it->second;
    }
    if (auto it = merged.find("hd_profile");
        it != merged.end() && !s.cli.hd) {
        s.hd_profile = it->second;
    }
    if (auto it = merged.find("render_scale");
        it != merged.end() && !s.cli.scale) {
        s.render_scale = std::atoi(it->second.c_str());
    }
    if (auto it = merged.find("game_dir");
        it != merged.end() && !s.cli.game_dir) {
        s.game_dir = it->second;
        s.config_game_dir = true;
    }
    // Audio keys (CLI defaults are sentinels; config fills them).
    // Gamepad mapping keys (config-only; SDL button names).
    if (auto it = merged.find("pad_jump"); it != merged.end())
        s.pad_jump = it->second;
    if (auto it = merged.find("pad_attack"); it != merged.end())
        s.pad_attack = it->second;
    if (auto it = merged.find("pad_pause"); it != merged.end())
        s.pad_pause = it->second;
    if (auto it = merged.find("pad_confirm"); it != merged.end())
        s.pad_confirm = it->second;
    if (auto it = merged.find("pad_back"); it != merged.end())
        s.pad_back = it->second;
    if (auto it = merged.find("pad_deadzone"); it != merged.end())
        s.pad_deadzone = std::atoi(it->second.c_str());
    if (auto it = merged.find("music_device");
        it != merged.end() && s.music_device == "auto") {
        s.music_device = it->second;
    }
    if (auto it = merged.find("rom_dir");
        it != merged.end() && s.rom_dir.empty()) {
        s.rom_dir = it->second;
    }
    if (auto it = merged.find("soundfont");
        it != merged.end() && s.soundfont.empty()) {
        s.soundfont = it->second;
    }
    if (auto it = merged.find("sfx_backend");
        it != merged.end() && s.sfx_backend == "auto") {
        s.sfx_backend = it->second;
    }
    // Tuning keys (sentinel defaults; config fills them when the CLI left
    // the default).  Read-only from config, like the audio keys above —
    // bool flags can't tell "default off" from "explicitly off", so they
    // (vsync/fullscreen) are CLI-only by design.
    if (auto it = merged.find("display_mode");
        it != merged.end() && s.display_mode == "gpu") {
        s.display_mode = it->second;
    }
    if (auto it = merged.find("audio_rate");
        it != merged.end() && s.audio_rate == 0) {
        s.audio_rate = std::atoi(it->second.c_str());
    }
    if (auto it = merged.find("audio_buffer");
        it != merged.end() && s.audio_buffer == 0) {
        s.audio_buffer = std::atoi(it->second.c_str());
    }
    if (auto it = merged.find("transitions");
        it != merged.end() && s.transitions == "smooth") {
        s.transitions = it->second;
    }
    if (auto it = merged.find("aspect");
        it != merged.end() && !s.cli.aspect) {
        // Guard on the FLAG, not the sentinel value: an explicit
        // "--aspect keep" must beat a saved widescreen config
        // (0.9.2 field-bug regression).
        s.aspect = it->second;
    }
    if (auto it = merged.find("hd_font");
        it != merged.end() && s.hd_font == "freckle") {
        s.hd_font = it->second;
    }
    if (auto it = merged.find("banner_fx");
        it != merged.end() && s.banner_fx == "caveman") {
        s.banner_fx = it->second;
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
    if (auto it = pc.find("enhance"); it != pc.end() && !s.cli.enhanced)
        s.enhance_list = it->second;
    if (auto it = pc.find("hd_profile"); it != pc.end() && !s.cli.hd)
        s.hd_profile = it->second;
    if (auto it = pc.find("render_scale"); it != pc.end() && !s.cli.scale)
        s.render_scale = std::atoi(it->second.c_str());
    if (auto it = pc.find("aspect"); it != pc.end() && !s.cli.aspect)
        s.aspect = it->second;
}

}  // namespace olduvai::app
