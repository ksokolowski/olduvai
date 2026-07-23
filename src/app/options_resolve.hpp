// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure resolution of the config-influenced play settings across the
// documented precedence: defaults < play.json < --profile < CLI flags
// (CC3 phase 3 — extracted from main.cpp, where every one of the five
// 0.9.2 field bugs lived untested: the aspect-guard, the dropped
// first-run choice, the ask-once gating, style_answered, the config-path
// fallout).  No I/O here: the caller loads/saves play.json and applies
// the profile overlay; these functions only decide who wins.

#pragma once

#include <string>

#include "config.hpp"

namespace olduvai::app {

// The subset of the play settings the saved config can influence, plus
// which of them the command line explicitly stated (`cli`).  Defaults
// here ARE the engine defaults — the parse loop writes CLI values over
// them, then merge_config() lets the config fill what the CLI left.
struct PlaySettings {
    bool vga_scan = true;
    std::string autofire = "off";
    bool enhanced = false;
    std::string enhance_list;            // --enhance a,b,c (granular subset)
    std::string hd_profile;
    int render_scale = 4;                // default 4 → omniscale 1280x800
    std::string game_dir;                // string mirror of main's fs::path
    std::string pad_jump = "a", pad_attack = "x", pad_pause = "start",
                pad_confirm = "a", pad_back = "b";
    int pad_deadzone = 8000;
    std::string music_device = "auto";
    std::string rom_dir;
    std::string soundfont;
    std::string sfx_backend = "auto";    // pair to music device
    std::string display_mode = "gpu";    // gpu (GPU scale) | cpu (software)
    int audio_rate = 0;                  // 0 = device default / auto
    int audio_buffer = 0;                // 0 = 2048-frame default
    std::string transitions = "smooth";  // smooth | classic
    std::string aspect = "keep";         // keep | 4:3 | stretch | widescreen
    std::string hd_font = "freckle";     // freckle | noto
    std::string banner_fx = "caveman";   // caveman|fire|rainbow|gold|pulse

    // What the command line explicitly stated (guards: CLI beats config).
    struct Cli {
        bool vga_scan = false;
        bool autofire = false;
        bool enhanced = false;
        bool hd = false;
        bool scale = false;
        bool aspect = false;
        bool game_dir = false;
    } cli;

    // Outputs of merge_config().
    bool config_game_dir = false;   // game_dir came from the config file
    bool style_answered = false;    // config/profile/CLI ever chose Classic/HD
    std::string bug_report_dir;     // config-only; caller applies the side
                                    // effect (set_bug_report_dir is SDL-side)
};

// Fold the merged config (play.json with the --profile overlay already
// applied via apply_profile) into the settings.  Per-key guards match the
// long-standing rules: cli.* flags for the flagged keys, sentinel values
// for the audio/tuning keys, unconditional for the pad mapping.
void merge_config(PlaySettings& s, const Config& merged);

// Fold a first-run presentation choice ("hd"/"dos") into THIS session's
// settings — the saved config only helps the NEXT launch.  cli_profile /
// cli.* keep the documented precedence: an explicit flag or --profile
// always wins; an empty preset (box unavailable) is a no-op.
void adopt_preset(PlaySettings& s, const std::string& cli_profile,
                  const std::string& preset);

}  // namespace olduvai::app
