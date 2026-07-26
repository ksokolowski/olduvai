// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "options_build.hpp"

#include <filesystem>
#include <sstream>
#include <string>

#include "config.hpp"                  // Config, load/save_config_file, config_path
#include "enhance/upscale.hpp"         // is_supported_hd_profile, supported_hd_profiles
#include "presentation/enhance_flags.hpp"

namespace olduvai::app {

BuildOutcome build_game_options(const CliArgs& args, PlaySettings& ps,
                                olduvai::presentation::GameOptions& out) {
    BuildOutcome oc;

    // Build the enhanced-feature set.  --enhanced enables the full bundle;
    // --enhance a,b enables a named subset (union with --enhanced).  Mirrors
    // reference tools/play.py _ENHANCEMENT_NAMES.  An unknown name is a hard
    // error (exit 2), matching the reference.
    olduvai::presentation::EnhanceFlags enhance_flags;
    if (ps.enhanced)
        enhance_flags = olduvai::presentation::EnhanceFlags::all();
    {
        std::stringstream ss(ps.enhance_list);
        std::string item;
        while (std::getline(ss, item, ',')) {
            const auto b = item.find_first_not_of(" \t");
            if (b == std::string::npos) continue;
            const auto e = item.find_last_not_of(" \t");
            const std::string name = item.substr(b, e - b + 1);
            if      (name == "smooth-motion") enhance_flags.smooth_motion = true;
            else if (name == "cinematic-cue") enhance_flags.cinematic_cue = true;
            else if (name == "hud-overlay")   enhance_flags.hud_overlay   = true;
            else if (name == "fluid-bubbles") enhance_flags.fluid_bubbles = true;
            else if (name == "secret-slide")  enhance_flags.secret_slide  = true;
            else if (name == "descent-pan")   enhance_flags.descent_pan   = true;
            else if (name == "hd-text")       enhance_flags.hd_text       = true;
            else {
                oc.ok = false;
                oc.exit_code = 2;
                oc.error =
                    "olduvai: unknown --enhance feature '" + name +
                    "'.  Known: smooth-motion, cinematic-cue, hud-overlay, "
                    "fluid-bubbles, secret-slide, descent-pan, hd-text\n";
                return oc;
            }
        }
    }
    // Umbrella `enhanced` = the HD-render substrate; on if any feature is
    // requested (so a single --enhance gives the enhanced render path).
    ps.enhanced = enhance_flags.any();

    // ── Tuning-flag validation (reject typos like the reference does).
    if (ps.display_mode != "gpu" && ps.display_mode != "cpu") {
        oc.ok = false;
        oc.exit_code = 2;
        oc.error =
            "olduvai: --display-mode must be 'gpu' or 'cpu' (got '" +
            ps.display_mode + "')\n";
        return oc;
    }
    if (ps.transitions != "smooth" && ps.transitions != "classic") {
        oc.ok = false;
        oc.exit_code = 2;
        oc.error =
            "olduvai: --transitions must be 'smooth' or 'classic' (got '" +
            ps.transitions + "')\n";
        return oc;
    }
    if (ps.aspect != "keep" && ps.aspect != "4:3" &&
        ps.aspect != "stretch" && ps.aspect != "widescreen") {
        oc.ok = false;
        oc.exit_code = 2;
        oc.error =
            "olduvai: --aspect must be 'keep', '4:3', 'stretch', or "
            "'widescreen' (got '" + ps.aspect + "')\n";
        return oc;
    }
    // --hd-font: map the friendly name to the bundled TTF file.  Unknown
    // names are a hard error (matches the reference choices=[...]).
    std::string hd_font_file;
    if (ps.hd_font == "freckle") {
        hd_font_file = "FreckleFace-Regular.ttf";
    } else if (ps.hd_font == "noto") {
        hd_font_file = "NotoSans-Regular.ttf";
    } else {
        oc.ok = false;
        oc.exit_code = 2;
        oc.error =
            "olduvai: --hd-font must be 'freckle' or 'noto' (got '" +
            ps.hd_font + "')\n";
        return oc;
    }
    // --banner-fx: reject unknown effect names (the shader would silently
    // fall back to caveman otherwise).
    if (ps.banner_fx != "caveman" && ps.banner_fx != "fire" &&
        ps.banner_fx != "rainbow" && ps.banner_fx != "gold" &&
        ps.banner_fx != "pulse") {
        oc.ok = false;
        oc.exit_code = 2;
        oc.error =
            "olduvai: --banner-fx must be caveman|fire|rainbow|gold|pulse "
            "(got '" + ps.banner_fx + "')\n";
        return oc;
    }
    // --hd-profile: empty means "use the default" (omniscale, the
    // historical owner default).  Any non-empty name must be one olduvai
    // actually renders — reject unknown/unimplemented names with exit 2
    // and the supported list (matches how the reference raises KeyError;
    // NO silent fall-through to omniscale for a name we don't implement).
    if (ps.hd_profile.empty()) {
        ps.hd_profile = "omniscale";
    } else if (!olduvai::enhance::is_supported_hd_profile(ps.hd_profile)) {
        std::string list;
        for (const auto& p : olduvai::enhance::supported_hd_profiles()) {
            if (!list.empty()) list += ", ";
            list += p;
        }
        oc.ok = false;
        oc.exit_code = 2;
        oc.error =
            "olduvai: --hd-profile '" + ps.hd_profile +
            "' is not supported.  Supported profiles: " + list + ".\n";
        return oc;
    }

    // --transitions classic: force smooth-motion off (a convenience
    // override of the enhance flag, applied AFTER enhance resolution so
    // it wins).
    //
    // --trace needs exactly ONE presented state per logic frame, but
    // smooth-motion inserts sub-frames whose count comes from the display
    // refresh (non-deterministic) — so tracing forces classic.  --replay
    // ALONE does NOT: it only injects inputs once per logic tick, which
    // sub-frame render interpolation never touches, so a recorded session
    // replays with full smooth motion (demo-movie friendly).  --replay
    // WITH --trace still forces classic via the trace branch.
    if (ps.transitions == "classic") {
        enhance_flags.smooth_motion = false;
    } else if (!args.play_trace.empty()) {
        if (enhance_flags.smooth_motion) {
            oc.warnings.push_back(
                "olduvai: --trace set → forcing transitions classic "
                "(smooth-motion off) for deterministic frames\n");
        }
        enhance_flags.smooth_motion = false;
    }

    // widescreen_active requires the HD substrate (enhanced AND a non-
    // native hd profile) — anything else silently pillarboxed before;
    // say so instead.
    if (ps.aspect == "widescreen" &&
        (!ps.enhanced || ps.hd_profile == "native")) {
        oc.warnings.push_back(
            "olduvai: --aspect widescreen needs the enhanced HD substrate "
            "(--enhanced or an --enhance subset, plus a non-native "
            "--hd-profile) — falling back to a plain pillarbox\n");
    }

    olduvai::presentation::GameOptions& go = out;
    go.game_dir = args.game_dir;
    // Sequencer mapping (EXE FUN_2bd7_04be slots): no --level → position 0
    // (the attract: intro cards + title + main menu); an explicit 1-7
    // jumps straight into that level (no logo/title/menu — --level 1 is
    // explicit too); 8 = the win ending.  Headless/replay runs remap 0→1
    // inside run_game for deterministic gameplay frame 0.
    go.level = args.play_level < 0 ? 0 : args.play_level;
    go.pad_jump = ps.pad_jump;
    go.pad_attack = ps.pad_attack;
    go.pad_pause = ps.pad_pause;
    go.pad_confirm = ps.pad_confirm;
    go.pad_back = ps.pad_back;
    go.pad_deadzone = ps.pad_deadzone;
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
    go.hd_font = hd_font_file;
    // In-game Options menu → play.json.  Load-modify-save keeps any keys
    // the menu doesn't touch; the app layer owns config I/O.
    go.persist = [](const std::string& key, const std::string& value) {
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
