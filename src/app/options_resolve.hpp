// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure resolution of the config-influenced play settings: defaults <
// --default-profile < play.json < --profile < CLI flags.  No I/O; the caller
// loads/saves play.json and applies the profile overlay.

#pragma once

#include <string>
#include <vector>

#include "config.hpp"

namespace olduvai::app {

// The settings the saved config can influence, plus which the command line
// stated (`cli`).  Defaults are the engine defaults: the parser writes CLI
// values over them, then merge_config() fills what the CLI left.
struct PlaySettings {
    bool vga_scan = true;
    std::string autofire = "off";
    bool enhanced = false;
    std::string enhance_list;            // --enhance a,b,c (granular subset)
    // enhance_list came from play.json: a stale token there must never stop the
    // game (a typo on the command line is rejected).
    bool enhance_list_from_config = false;
    std::string hd_profile;
    int render_scale = 4;                // default 4 → omniscale 1280x800
    std::string game_dir;                // string mirror of main's fs::path
    std::string pad_jump = "a", pad_attack = "x", pad_pause = "start",
                pad_confirm = "a", pad_back = "b";
    int pad_deadzone = 8000;
    std::string music_device = "auto";
    std::string rom_dir;
    // MT-32 vs CM-32L: different machines, different sound.
    std::string mt32_model;   // "" = auto
    std::string soundfont;
    std::string sfx_backend = "auto";    // pair to music device
    std::string display_mode = "gpu";    // gpu (GPU scale) | cpu (software)
    int audio_rate = 0;                  // 0 = device default / auto
    int audio_buffer = 0;                // 0 = 2048-frame default
    std::string transitions = "smooth";  // smooth | classic
    std::string aspect = "keep";         // keep | 4:3 | stretch | widescreen
    std::string hd_font = "freckle";     // freckle | noto
    std::string banner_fx = "caveman";   // caveman|fire|rainbow|gold|pulse
    // Smooth-present tuning (config-only: no CLI flag, no menu row).  Kept as
    // the raw config text; build_game_options validates, warns, falls back.
    std::string smooth_subframes = "0";   // 0 = auto | 1..12
    std::string smooth_vsync = "auto";    // auto | off

    // What the command line explicitly stated (guards: CLI beats config).
    struct Cli {
        bool vga_scan = false;
        bool autofire = false;
        bool enhanced = false;
        bool hd = false;
        bool scale = false;
        bool aspect = false;
        bool game_dir = false;
        // These cannot be inferred by comparing to the default (the parser
        // writes real defaults), so an explicit `--music-device auto` must be
        // recorded or a saved play.json would win.
        bool music_device = false;
        bool sfx_backend = false;
        bool display_mode = false;
        bool transitions = false;
        bool hd_font = false;
        bool banner_fx = false;
    } cli;

    // Outputs of merge_config().
    bool config_game_dir = false;   // game_dir came from the config file
    bool style_answered = false;    // config/profile/CLI ever chose Classic/HD
    std::string profile_family = "desktop";   // set from layer_config()
    std::string bug_report_dir;     // config-only; caller applies the side
                                    // effect (set_bug_report_dir is SDL-side)
};

// Fold the merged config (play.json + --profile overlay) into the settings:
// cli.* guards for flagged keys, sentinels for audio/tuning keys,
// unconditional for the pad mapping.
void merge_config(PlaySettings& s, const Config& merged);

// Fold one profile pin through its CLI guard.  False for an unknown key (a
// test checks every pin of every profile is adoptable).
bool adopt_profile_key(PlaySettings& s, const std::string& key,
                       const std::string& value);

// Fold a first-run choice ("hd"/"dos") into this session (the saved config
// only affects the next launch).  An explicit flag or --profile still wins;
// an empty preset is a no-op.
void adopt_preset(PlaySettings& s, const std::string& cli_profile,
                  const std::string& preset);

// Config layers below the CLI: engine defaults < --default-profile <
// play.json < --profile.  `family`: of --profile if given, else of
// --default-profile, else desktop.  An unknown default-profile warns.
struct LayeredConfig {
    Config merged;
    std::string family;
    std::vector<std::string> warnings;
};
LayeredConfig layer_config(const Config& file_cfg,
                           const std::string& cli_profile,
                           const std::string& default_profile);

// What --save-config writes: the saved file, the explicit --profile and the
// CLI-stated keys, never the --default-profile layer (device defaults must not
// freeze into play.json).
Config config_to_save(const Config& file_cfg, const std::string& cli_profile,
                      const PlaySettings& ps, const std::string& game_dir);

}  // namespace olduvai::app
