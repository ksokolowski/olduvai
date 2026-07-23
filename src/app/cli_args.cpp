// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "cli_args.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace olduvai::app {

namespace fs = std::filesystem;

ParseOutcome parse_args(int argc, char** argv, CliArgs& args, PlaySettings& ps) {
    // Alias the result fields so the parse loop below is the verbatim lexer
    // that used to live in main() — every "--flag" string is untouched.
    auto& game_dir = args.game_dir;
    auto& viewer = args.viewer;
    auto& play = args.play;
    auto& do_prepare = args.do_prepare;
    auto& do_decode_sfx = args.do_decode_sfx;
    auto& do_verify_cache = args.do_verify_cache;
    auto& do_purge_cache = args.do_purge_cache;
    auto& do_list_midi_ports = args.do_list_midi_ports;
    auto& play_level = args.play_level;
    auto& play_midi_port = args.play_midi_port;
    auto& play_replay = args.play_replay;
    auto& play_trace = args.play_trace;
    auto& play_record_inputs = args.play_record_inputs;
    auto& play_cheats = args.play_cheats;
    auto& play_god = args.play_god;
    auto& play_debug_collision = args.play_debug_collision;
    auto& play_debug_entities = args.play_debug_entities;
    auto& play_debug_perf = args.play_debug_perf;
    auto& play_vsync = args.play_vsync;
    auto& play_fullscreen = args.play_fullscreen;
    auto& play_window_w = args.play_window_w;
    auto& play_window_h = args.play_window_h;
    auto& play_start_screen = args.play_start_screen;
    auto& profile = args.profile;
    auto& no_config = args.no_config;
    auto& save_config = args.save_config;
    auto& play_frames = args.play_frames;
    auto& play_shot = args.play_shot;
    auto& play_shot_frame = args.play_shot_frame;
    auto& viewer_frames = args.viewer_frames;
    auto& viewer_shot = args.viewer_shot;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        bool known = true;
        if (arg == "--help" || arg == "-h") {
            return {true, 0, true, false};
        } else if (arg == "--version") {
            return {true, 0, false, true};
        } else if (arg == "--game-dir" && i + 1 < argc) {
            game_dir = argv[++i];
            ps.cli.game_dir = true;
        } else if (arg == "--profile" && i + 1 < argc) {
            profile = argv[++i];
            if (profile != "dos" && profile != "hd" && profile != "hd-43") {
                std::fprintf(stderr,
                    "olduvai: --profile must be 'dos', 'hd', or 'hd-43' "
                    "(got '%s')\n", profile.c_str());
                return {true, 2, false, false};
            }
        } else if (arg == "--no-config") {
            no_config = true;
        } else if (arg == "--save-config") {
            save_config = true;
        } else if (arg == "--viewer") {
            viewer = true;
        } else if (arg == "--play") {
            play = true;
        } else if (arg == "--prepare") {
            do_prepare = true;
        } else if (arg == "--decode-sfx") {
            do_decode_sfx = true;
        } else if (arg == "--verify-cache") {
            do_verify_cache = true;
        } else if (arg == "--purge-cache") {
            do_purge_cache = true;
        } else if (arg == "--level" && i + 1 < argc) {
            play_level = std::atoi(argv[++i]);
            if (play_level < 0 || play_level > 8) {
                std::fprintf(stderr,
                    "olduvai: --level must be 0 (intro/title), 1-7 (play "
                    "levels, display numbering) or 8 (win ending) "
                    "(got '%s')\n", argv[i]);
                return {true, 2, false, false};
            }
        } else if (arg == "--enhanced") {
            ps.enhanced = true;
            ps.cli.enhanced = true;
        } else if (arg == "--enhance" && i + 1 < argc) {
            ps.enhance_list = argv[++i];
            ps.cli.enhanced = true;   // an explicit subset also overrides config
        } else if (arg == "--hd-profile" && i + 1 < argc) {
            ps.hd_profile = argv[++i];
            ps.cli.hd = true;
        } else if (arg == "--music-device" && i + 1 < argc) {
            ps.music_device = argv[++i];
        } else if (arg == "--midi-port" && i + 1 < argc) {
            play_midi_port = argv[++i];
        } else if (arg == "--list-midi-ports") {
            do_list_midi_ports = true;
        } else if (arg == "--replay" && i + 1 < argc) {
            play_replay = argv[++i];
        } else if (arg == "--trace" && i + 1 < argc) {
            play_trace = argv[++i];
        } else if (arg == "--record-inputs" && i + 1 < argc) {
            play_record_inputs = argv[++i];
        } else if (arg == "--cheats") {
            play_cheats = true;
        } else if (arg == "--god") {
            play_god = true;
        } else if (arg == "--autofire") {
            ps.autofire = "fast";
            ps.cli.autofire = true;
            if (i + 1 < argc) {
                const std::string s = argv[i + 1];
                if (s == "slow" || s == "medium" || s == "fast") {
                    ps.autofire = s;
                    ++i;
                }
            }
        } else if (arg == "--no-autofire") {
            ps.autofire = "off";
            ps.cli.autofire = true;
        } else if (arg == "--debug-collision") {
            play_debug_collision = true;
        } else if (arg == "--debug-entities") {
            play_debug_entities = true;
        } else if (arg == "--debug-perf") {
            play_debug_perf = true;
        } else if (arg == "--vga-scan") {
            ps.vga_scan = true;
            ps.cli.vga_scan = true;
        } else if (arg == "--no-vga-scan") {
            ps.vga_scan = false;
            ps.cli.vga_scan = true;
        } else if (arg == "--vsync") {
            play_vsync = true;
        } else if (arg == "-f" || arg == "--fullscreen") {
            play_fullscreen = true;
        } else if (arg == "--display-mode" && i + 1 < argc) {
            ps.display_mode = argv[++i];
        } else if (arg == "--audio-rate" && i + 1 < argc) {
            ps.audio_rate = std::atoi(argv[++i]);
        } else if (arg == "--audio-buffer" && i + 1 < argc) {
            ps.audio_buffer = std::atoi(argv[++i]);
        } else if (arg == "--transitions" && i + 1 < argc) {
            ps.transitions = argv[++i];
        } else if (arg == "--aspect" && i + 1 < argc) {
            ps.aspect = argv[++i];
            ps.cli.aspect = true;
        } else if (arg == "--hd-font" && i + 1 < argc) {
            ps.hd_font = argv[++i];
        } else if (arg == "--banner-fx" && i + 1 < argc) {
            ps.banner_fx = argv[++i];
        } else if (arg == "--start-screen" && i + 1 < argc) {
            play_start_screen = std::atoi(argv[++i]);
        } else if (arg == "--window" && i + 1 < argc) {
            const std::string wh = argv[++i];
            const auto xpos = wh.find_first_of("xX");
            if (xpos != std::string::npos) {
                play_window_w = std::atoi(wh.substr(0, xpos).c_str());
                play_window_h = std::atoi(wh.substr(xpos + 1).c_str());
            }
        } else if (arg == "--rom-dir" && i + 1 < argc) {
            ps.rom_dir = argv[++i];
        } else if (arg == "--soundfont" && i + 1 < argc) {
            ps.soundfont = argv[++i];
        } else if (arg == "--sfx-backend" && i + 1 < argc) {
            ps.sfx_backend = argv[++i];
        } else if (arg == "--render-scale" && i + 1 < argc) {
            ps.render_scale = std::atoi(argv[++i]);
            ps.cli.scale = true;
        } else if (arg == "--play-frames" && i + 1 < argc) {
            play_frames = std::atoi(argv[++i]);
        } else if (arg == "--play-shot" && i + 1 < argc) {
            play_shot = argv[++i];
        } else if (arg == "--play-shot-frame" && i + 1 < argc) {
            play_shot_frame = std::atoi(argv[++i]);
        } else if (arg == "--viewer-frames" && i + 1 < argc) {
            viewer_frames = std::atoi(argv[++i]);
        } else if (arg == "--viewer-shot" && i + 1 < argc) {
            viewer_shot = argv[++i];
        } else {
            known = false;
        }
        if (!known) {
            std::fprintf(stderr, "olduvai: unrecognized argument '%s'\n",
                         arg.c_str());
            std::fprintf(stderr, "Try 'olduvai --help' for usage.\n");
            return {true, 2, false, false};
        }
    }
    return {false, 0, false, false};
}

}  // namespace olduvai::app
