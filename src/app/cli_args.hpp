// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Command-line parsing: the ~40-flag argv lexer, lifted out of main() into a
// testable unit (SOC roadmap: cli_args).  CliArgs bundles the bootstrap
// locals main() used to declare loose; parse_args fills it plus the
// PlaySettings CLI fields, and reports help/version/errors via ParseOutcome
// so parse_args itself does no stdout (main renders help/version).
#pragma once

#include <filesystem>
#include <string>

#include "options_resolve.hpp"   // PlaySettings

namespace olduvai::app {

struct CliArgs {
    std::filesystem::path game_dir = ".";
    bool viewer = false;
    bool play = false;
    bool do_prepare = false;
    bool do_decode_sfx = false;
    bool do_verify_cache = false;
    bool do_purge_cache = false;
    bool do_list_midi_ports = false;
    int play_level = -1;
    std::string play_midi_port;
    std::string play_replay;
    std::string play_trace;
    std::string play_record_inputs;
    bool play_cheats = false;
    bool play_god = false;
    bool play_debug_collision = false;
    bool play_debug_entities = false;
    bool play_debug_perf = false;
    bool play_vsync = false;
    bool play_fullscreen = false;
    int play_window_w = 0;
    int play_window_h = 0;   // --window WxH (0 = auto)
    int play_start_screen = 0;
    std::string profile;
    bool no_config = false;
    bool save_config = false;
    int play_frames = -1;
    std::string play_shot;
    int play_shot_frame = 1;
    int viewer_frames = -1;
    std::string viewer_shot;
};

// What main() should do after parsing.  When should_exit is set, main returns
// exit_code — first rendering help/version if the flag is set.
struct ParseOutcome {
    bool should_exit = false;
    int exit_code = 0;
    bool show_help = false;
    bool show_version = false;
};

// Lex argv into `args` (+ the CLI-flagged PlaySettings fields).  No stdout;
// error messages go to stderr and set should_exit + exit_code = 2.
ParseOutcome parse_args(int argc, char** argv, CliArgs& args, PlaySettings& ps);

}  // namespace olduvai::app
