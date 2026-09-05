// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "cli_args.hpp"

#include <cstdio>
#include "parse_num.hpp"
#include <string>

namespace olduvai::app {


ParseOutcome parse_args(int argc, char** argv, CliArgs& args, PlaySettings& ps) {
    // argv numbers are explicit user intent: report and fail rather than let
    // atoi's silent 0 through.  Returns false once it has printed the reason.
    const auto num = [](const char* flag, const char* text, int& dst) -> bool {
        if (parse_int(text, dst)) return true;
        std::fprintf(stderr,
            "olduvai: %s expects a whole number (got '%s')\n", flag, text);
        return false;
    };
    // Alias the result fields so the parse loop below is the verbatim lexer
    // that used to live in main() — every "--flag" string is untouched.
    auto& game_dir = args.game_dir;
    auto& viewer = args.viewer;
    auto& play = args.play;
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
    auto& render_audio = args.render_audio;
    auto& render_audio_out = args.render_audio_out;
    auto& render_audio_secs = args.render_audio_secs;

    // Three argument shapes repeat verbatim down the chain below — a
    // whole-number flag that parses strictly and exits 2 naming the flag,
    // a settings string that stamps its cli.* override, and a plain string
    // store.  Each used to be its own else-if (~2-3 complexity points plus
    // nesting apiece); a table walk per shape replaces them (§3.10, the
    // move merge_config's families went).  A shape match REQUIRES a value
    // to be present (i + 1 < argc) — without one the name falls through to
    // the unknown-argument handler, exactly as the chained form did.
    struct IntFlag { const char* name; int* dst; bool* cli; };
    const IntFlag kIntFlags[] = {
        {"--audio-rate", &ps.audio_rate, nullptr},
        {"--audio-buffer", &ps.audio_buffer, nullptr},
        {"--start-screen", &play_start_screen, nullptr},
        {"--render-scale", &ps.render_scale, &ps.cli.scale},
        {"--play-frames", &play_frames, nullptr},
        {"--play-shot-frame", &play_shot_frame, nullptr},
        {"--viewer-frames", &viewer_frames, nullptr},
    };
    struct StrFlag { const char* name; std::string* dst; bool* cli; };
    const StrFlag kStrFlags[] = {
        {"--display-mode", &ps.display_mode, &ps.cli.display_mode},
        {"--aspect", &ps.aspect, &ps.cli.aspect},
        {"--hd-font", &ps.hd_font, &ps.cli.hd_font},
        {"--banner-fx", &ps.banner_fx, &ps.cli.banner_fx},
        {"--transitions", &ps.transitions, &ps.cli.transitions},
        {"--hd-profile", &ps.hd_profile, &ps.cli.hd},
        {"--music-device", &ps.music_device, &ps.cli.music_device},
        {"--sfx-backend", &ps.sfx_backend, &ps.cli.sfx_backend},
    };
    struct PlainStrFlag { const char* name; std::string* dst; };
    const PlainStrFlag kPlainStrFlags[] = {
        {"--midi-port", &play_midi_port},
        {"--replay", &play_replay},
        {"--trace", &play_trace},
        {"--record-inputs", &play_record_inputs},
        {"--rom-dir", &ps.rom_dir},
        {"--mt32-model", &ps.mt32_model},
        {"--soundfont", &ps.soundfont},
        {"--play-shot", &play_shot},
        {"--viewer-shot", &viewer_shot},
        {"--render-audio", &render_audio},
        {"--render-audio-out", &render_audio_out},
    };
    const auto find_int = [&](const std::string& a) -> const IntFlag* {
        for (const auto& f : kIntFlags)
            if (a == f.name) return &f;
        return nullptr;
    };
    const auto find_str = [&](const std::string& a) -> const StrFlag* {
        for (const auto& f : kStrFlags)
            if (a == f.name) return &f;
        return nullptr;
    };
    const auto find_plain = [&](const std::string& a) -> const PlainStrFlag* {
        for (const auto& f : kPlainStrFlags)
            if (a == f.name) return &f;
        return nullptr;
    };
    // Boolean flags: one name, one destination, one value to store.  The
    // cli.* stamp is only wanted where a saved config key would otherwise
    // win — the args.* session fields never read config, so they pass
    // nullptr and take the value unconditionally.
    struct BoolFlag { const char* name; bool* dst; bool value; bool* cli; };
    const BoolFlag kBoolFlags[] = {
        {"--no-config", &no_config, true, nullptr},
        {"--save-config", &save_config, true, nullptr},
        {"--viewer", &viewer, true, nullptr},
        {"--play", &play, true, nullptr},
        {"--list-midi-ports", &do_list_midi_ports, true, nullptr},
        {"--cheats", &play_cheats, true, nullptr},
        {"--god", &play_god, true, nullptr},
        {"--debug-collision", &play_debug_collision, true, nullptr},
        {"--debug-entities", &play_debug_entities, true, nullptr},
        {"--debug-perf", &play_debug_perf, true, nullptr},
        {"--vga-scan", &ps.vga_scan, true, &ps.cli.vga_scan},
        {"--no-vga-scan", &ps.vga_scan, false, &ps.cli.vga_scan},
        {"--vsync", &play_vsync, true, nullptr},
        {"-f", &play_fullscreen, true, nullptr},
        {"--fullscreen", &play_fullscreen, true, nullptr},
        {"--enhanced", &ps.enhanced, true, &ps.cli.enhanced},
    };
    const auto find_bool = [&](const std::string& a) -> const BoolFlag* {
        for (const auto& f : kBoolFlags)
            if (a == f.name) return &f;
        return nullptr;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        bool known = true;
        if (arg == "--help" || arg == "-h") {
            return {true, 0, true, false};
        } else if (arg == "--version") {
            return {true, 0, false, true};
        } else if (const IntFlag* f =
                       i + 1 < argc ? find_int(arg) : nullptr) {
            if (!num(f->name, argv[++i], *f->dst))
                return {true, 2, false, false};
            if (f->cli != nullptr) *f->cli = true;
        } else if (const StrFlag* f =
                       i + 1 < argc ? find_str(arg) : nullptr) {
            *f->dst = argv[++i];
            *f->cli = true;
        } else if (const PlainStrFlag* f =
                       i + 1 < argc ? find_plain(arg) : nullptr) {
            *f->dst = argv[++i];
        } else if (const BoolFlag* f = find_bool(arg)) {
            *f->dst = f->value;
            if (f->cli != nullptr) *f->cli = true;
        } else if (arg == "--game-dir" && i + 1 < argc) {
            game_dir = argv[++i];
            ps.cli.game_dir = true;
        } else if (arg == "--profile" && i + 1 < argc) {
            profile = argv[++i];
            // hd-43 was a profile that differed from hd only by aspect.  It
            // stays accepted — it is a documented CLI value and someone's
            // shell alias — but it now resolves to what it always meant.
            if (profile == "hd-43") {
                profile = "hd";
                ps.aspect = "4:3";
                ps.cli.aspect = true;   // explicit: must beat a saved config
            }
            if (profile != "dos" && profile != "hd") {
                std::fprintf(stderr,
                    "olduvai: --profile must be 'dos' or 'hd' "
                    "(got '%s')\n", profile.c_str());
                return {true, 2, false, false};
            }
        } else if (arg == "--level" && i + 1 < argc) {
            if (!num("--level", argv[++i], play_level))
                return {true, 2, false, false};
            if (play_level < 0 || play_level > 8) {
                std::fprintf(stderr,
                    "olduvai: --level must be 0 (intro/title), 1-7 (play "
                    "levels, display numbering) or 8 (win ending) "
                    "(got '%s')\n", argv[i]);
                return {true, 2, false, false};
            }
        } else if (arg == "--enhance" && i + 1 < argc) {
            ps.enhance_list = argv[++i];
            ps.cli.enhanced = true;   // an explicit subset also overrides config
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
        } else if (arg == "--window" && i + 1 < argc) {
            const std::string wh = argv[++i];
            const auto xpos = wh.find_first_of("xX");
            if (xpos == std::string::npos ||
                !parse_int(wh.substr(0, xpos), play_window_w) ||
                !parse_int(wh.substr(xpos + 1), play_window_h)) {
                std::fprintf(stderr,
                    "olduvai: --window expects WxH, e.g. 896x400 (got '%s')\n",
                    wh.c_str());
                return {true, 2, false, false};
            }
        } else if (arg == "--render-sfx" && i + 1 < argc) {
            args.render_sfx = argv[++i];
        } else if (arg == "--render-audio-secs" && i + 1 < argc) {
            if (!parse_double(argv[++i], render_audio_secs)) {
                std::fprintf(stderr,
                    "olduvai: --render-audio-secs expects a number "
                    "(got '%s')\n", argv[i]);
                return {true, 2, false, false};
            }
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
