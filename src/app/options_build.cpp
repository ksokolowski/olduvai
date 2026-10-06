// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "options_build.hpp"

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>

#include "config.hpp"                  // Config, load/save_config_file, config_path
#include "enhance/upscale.hpp"         // is_supported_hd_profile, supported_hd_profiles
#include "presentation/enhance_flags.hpp"
#include "presentation/input/button_layout.hpp"   // upgrade_round1_nintendo
#include "presentation/env_num.hpp"    // env_int — OLDUVAI_FORCE_SMOOTH gate escape
#include "presentation/render/smooth_config.hpp"  // smooth_* keys + persist refresh

namespace olduvai::app {

namespace {

// A fatal, exit-2 outcome; `error` carries its own newline.
bool fail(BuildOutcome& oc, std::string error) {
    oc.ok = false;
    oc.exit_code = 2;
    oc.error = std::move(error);
    return false;
}

bool one_of(const std::string& v, std::initializer_list<const char*> names) {
    return std::any_of(names.begin(), names.end(),
                       [&v](const char* n) { return v == n; });
}

// `--enhance a,b`: the legacy feature names are still parsed (play.json may
// hold them) but select nothing; any listed name means enhanced mode, because
// older menus saved `enhanced=false` plus a granular list.  An unknown name
// is fatal from the command line, a warning from the config file (the
// program wrote it, not the user).
bool parse_enhance_list(PlaySettings& ps, BuildOutcome& oc) {
    std::stringstream ss(ps.enhance_list);
    std::string item;
    bool any_listed = false;
    while (std::getline(ss, item, ',')) {
        const auto b = item.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        const auto e = item.find_last_not_of(" \t");
        const std::string name = item.substr(b, e - b + 1);
        if (one_of(name, {"smooth-motion", "cinematic-cue", "hud-overlay",
                          "fluid-bubbles", "secret-slide", "descent-pan",
                          "hd-text"})) {
            any_listed = true;
        } else if (ps.enhance_list_from_config) {
            oc.warnings.push_back("olduvai: ignoring unknown enhance feature '" +
                                  name + "' from the config file\n");
        } else {
            return fail(oc,
                        "olduvai: unknown --enhance feature '" + name +
                            "'.  Known: smooth-motion, cinematic-cue, "
                            "hud-overlay, fluid-bubbles, secret-slide, "
                            "descent-pan, hd-text\n");
        }
    }
    if (any_listed) ps.enhanced = true;
    return true;
}

// The tuning flags, rejected on a typo (as the reference does).  An empty
// --hd-profile means omniscale; an unknown one lists the supported set.
bool validate_choices(PlaySettings& ps, BuildOutcome& oc) {
    if (!one_of(ps.display_mode, {"gpu", "cpu"}))
        return fail(oc, "olduvai: --display-mode must be 'gpu' or 'cpu' (got '" +
                            ps.display_mode + "')\n");
    if (!one_of(ps.transitions, {"smooth", "classic"}))
        return fail(oc,
                    "olduvai: --transitions must be 'smooth' or 'classic' "
                    "(got '" + ps.transitions + "')\n");
    if (!one_of(ps.aspect, {"keep", "4:3", "stretch", "widescreen"}))
        return fail(oc,
                    "olduvai: --aspect must be 'keep', '4:3', 'stretch', or "
                    "'widescreen' (got '" + ps.aspect + "')\n");
    if (!one_of(ps.hd_font, {"freckle", "noto"}))
        return fail(oc, "olduvai: --hd-font must be 'freckle' or 'noto' (got '" +
                            ps.hd_font + "')\n");
    // The shader would silently fall back to caveman otherwise.
    if (!one_of(ps.banner_fx, {"caveman", "fire", "rainbow", "gold", "pulse"}))
        return fail(oc,
                    "olduvai: --banner-fx must be caveman|fire|rainbow|gold|"
                    "pulse (got '" + ps.banner_fx + "')\n");
    ps.hd_profile = olduvai::enhance::canonical_hd_profile(ps.hd_profile);
    if (ps.hd_profile.empty()) {
        ps.hd_profile = "omniscale";
    } else if (!olduvai::enhance::is_supported_hd_profile(ps.hd_profile)) {
        std::string list;
        for (const auto& p : olduvai::enhance::supported_hd_profiles())
            list += (list.empty() ? "" : ", ") + p;
        return fail(oc, "olduvai: --hd-profile '" + ps.hd_profile +
                            "' is not supported.  Supported profiles: " +
                            list + ".\n");
    }
    return true;
}

}  // namespace

BuildOutcome build_game_options(const CliArgs& args, PlaySettings& ps,
                                olduvai::presentation::GameOptions& out) {
    BuildOutcome oc;
    if (!parse_enhance_list(ps, oc) || !validate_choices(ps, oc)) return oc;

    // smooth_motion follows the umbrella.  --transitions classic turns it
    // off.  So does --trace, which needs one presented state per logic frame
    // (sub-frame counts follow the display); --replay alone does not, it
    // injects inputs per logic tick.
    olduvai::presentation::EnhanceFlags enhance_flags;
    enhance_flags.smooth_motion = ps.enhanced;
    if (ps.transitions == "classic") {
        enhance_flags.smooth_motion = false;
    } else if (!args.play_trace.empty() &&
               olduvai::presentation::env_int("OLDUVAI_FORCE_SMOOTH", 0) != 1) {
        if (enhance_flags.smooth_motion) {
            oc.warnings.emplace_back(
                "olduvai: --trace set → forcing transitions classic "
                "(smooth-motion off) for deterministic frames\n");
        }
        // The mode, not just the flag: a later re-derive from `enhanced` (a
        // pause Style reinit) would turn smooth motion back on mid-trace.
        ps.transitions = "classic";
        enhance_flags.smooth_motion = false;
    }
    // OLDUVAI_FORCE_SMOOTH=1 (boss_app) keeps smooth motion under --trace, for
    // golden_trace_l6_fight_hd only: boss traces pin per-logic-tick state,
    // paced by DosTicker.  Never for interactive sessions.

    // Widescreen needs the HD substrate (enhanced + a non-native profile); warn
    // instead of silently pillarboxing.
    if (ps.aspect == "widescreen" &&
        (!ps.enhanced || ps.hd_profile == "native")) {
        oc.warnings.emplace_back(
            "olduvai: --aspect widescreen needs the enhanced HD substrate "
            "(--enhanced or an --enhance subset, plus a non-native "
            "--hd-profile) — falling back to a plain pillarbox\n");
    }

    // Smooth-present tuning: config-only keys, so a bad value is a warning
    // that keeps auto — a broken config must never block playing.
    int smooth_subframes = 0;
    if (!olduvai::presentation::parse_smooth_subframes(ps.smooth_subframes,
                                                       smooth_subframes)) {
        oc.warnings.push_back(
            "olduvai: smooth_subframes must be 0-12 (got '" +
            ps.smooth_subframes + "') — using auto\n");
    }
    bool smooth_vsync_off = false;
    if (!olduvai::presentation::parse_smooth_vsync(ps.smooth_vsync,
                                                   smooth_vsync_off)) {
        oc.warnings.push_back(
            "olduvai: smooth_vsync must be 'auto' or 'off' (got '" +
            ps.smooth_vsync + "') — using auto\n");
    }

    olduvai::presentation::GameOptions& go = out;
    go.game_dir = args.game_dir;
    // Sequencer position (FUN_2bd7_04be slots): no --level -> 0 (attract); 1-7
    // jump into that level; 8 = the win ending.  Headless/replay remap 0 -> 1
    // in run_game.
    go.level = args.play_level < 0 ? 0 : args.play_level;
    presentation::PadBindings pad{ps.pad_jump, ps.pad_attack, ps.pad_confirm,
                                  ps.pad_back, ps.pad_pause};
    presentation::upgrade_round1_nintendo(pad);
    go.pad_jump = pad.jump;
    go.pad_attack = pad.attack;
    go.pad_pause = pad.pause;
    go.pad_confirm = pad.confirm;
    go.pad_back = pad.back;
    go.pad_deadzone = ps.pad_deadzone;
    go.key_left = ps.key_left;
    go.key_right = ps.key_right;
    go.key_up = ps.key_up;
    go.key_down = ps.key_down;
    go.key_attack = ps.key_attack;
    go.key_pause = ps.key_pause;
    go.key_quit = ps.key_quit;
    go.enhanced = ps.enhanced;
    go.enhance = enhance_flags;
    go.hd_profile = ps.hd_profile;
    go.banner_fx = ps.banner_fx;
    go.window_w = args.play_window_w;
    go.window_h = args.play_window_h;
    go.start_screen = args.play_start_screen;
    go.render_scale = ps.render_scale;
    go.music_device = ps.music_device;
    go.midi_port = args.play_midi_port;
    go.rom_dir = ps.rom_dir;
    go.mt32_model = ps.mt32_model;
    go.soundfont = ps.soundfont;
    go.sfx_backend = ps.sfx_backend;
    go.replay = args.play_replay;
    go.trace = args.play_trace;
    go.record_inputs = args.play_record_inputs;
    go.cheats = args.play_cheats;
    go.god = args.play_god;
    go.autofire = ps.autofire;
    go.debug_collision = args.play_debug_collision;
    go.debug_entities = args.play_debug_entities;
    go.debug_perf = args.play_debug_perf;
    go.frames = args.play_frames;
    go.screenshot = args.play_shot;
    go.screenshot_frame = args.play_shot_frame;
    // Scanout needs vblank pacing, but only CLASSIC uses it — the default-
    // on flag must not silently enable vsync for enhanced/HD runs.
    const bool hd_substrate = ps.enhanced && ps.hd_profile != "native";
    go.vsync = args.play_vsync || (ps.vga_scan && !hd_substrate);
    go.fullscreen = args.play_fullscreen;
    go.display_mode = ps.display_mode;
    go.audio_rate = ps.audio_rate;
    go.audio_buffer = ps.audio_buffer;
    go.transitions = ps.transitions;
    go.aspect = ps.aspect;
    go.vga_scan = ps.vga_scan;
    go.hd_font = ps.hd_font == "noto" ? "NotoSans-Regular.ttf"
                                      : "FreckleFace-Regular.ttf";
    go.smooth_subframes = smooth_subframes;
    go.smooth_vsync_off = smooth_vsync_off;
    go.profile_family = ps.profile_family;
    // In-game Options menu → play.json.  Load-modify-save keeps any keys
    // the menu doesn't touch; the app layer owns config I/O.
    go.persist = [](const std::string& key, const std::string& value) {
        // A smooth-present key applied from the Options menu must pace the
        // rebuild it rides with, not wait for the next launch.
        olduvai::presentation::apply_smooth_key(
            olduvai::presentation::smooth_present_config(), key, value);
        olduvai::app::Config c = olduvai::app::load_config_file();
        c[key] = value;
        olduvai::app::save_config_file(c);
    };
    // Quicksave alongside the config (…/olduvai/saves/quicksave.json).
    go.save_path = std::filesystem::path(olduvai::app::config_path())
                       .parent_path()
                       .append("saves")
                       .append("quicksave.sav")
                       .string();
    return oc;
}

}  // namespace olduvai::app
