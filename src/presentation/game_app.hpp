// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Playable game shell — M4 work-in-progress.  Currently: L1, single-screen
// scope (screen transitions and level flow land next).

#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "presentation/enhance_flags.hpp"
#include "presentation/play_config.hpp"

namespace olduvai::presentation {

// One session's options: the saved settings (PlayConfig) plus this run's
// command line and the app layer's wiring.
struct GameOptions : PlayConfig {
    std::filesystem::path game_dir;
    // Sequence position (FUN_2bd7_04be): 0 = attract, 1-7 = levels (display
    // numbering), 8 = win ending.  Default 1, so direct constructions (tests,
    // headless) start deterministic at frame 0; the CLI maps "no --level" to 0.
    int level = 1;
    // --start-screen N (debug): bind that surface screen at level entry
    // (clamped; no GET READY).  0 = normal start.
    int start_screen = 0;
    int frames = -1;          // exit after N frames (headless verification)
    std::string screenshot;   // save frame N (PNG by extension, else BMP) and exit
    int screenshot_frame = 1;
    // The individual Enhanced effects (`enhanced`, PlayConfig, is the HD
    // substrate; --enhanced sets them all).
    EnhanceFlags enhance;
    std::string midi_port;                // host-midi: MIDI OUT port name ("" = default)
    std::string replay;       // inputs.jsonl — scripted input replay
    // OLDUVAI_UNTHROTTLED=1 with a replay: the timedemo (pacing.hpp).
    bool unthrottled = false;
    std::string trace;        // out.jsonl — frame-state trace
    std::string record_inputs;  // out.jsonl — record live inputs (replay schema)
    bool cheats = false;      // --cheats: number keys 1-6 grant power-ups (test aid)
    bool god = false;         // --god: 99 lives, 999 energy, no death (debug).
    // Debug overlays (render aids, not reference-parity); off = untouched
    // rendering.
    bool debug_collision = false;  // tint solid collision cells
    bool debug_entities = false;   // box every active+visible entity
    bool debug_perf = false;       // FPS + frame-time text corner
    // ── Tuning knobs (mirror reference op play; default = byte-faithful).
    bool vsync = false;            // --vsync: SDL_RENDERER_PRESENTVSYNC
    // --vga-scan (classic): re-present the held frame every refresh between
    // logic ticks (VGA scanning VRAM at 70 Hz).  Same pixels; implies vsync.
    bool vga_scan = false;
    bool fullscreen = false;       // -f/--fullscreen: start desktop-fullscreen
    // --hd-font: vector face file for HD text (freckle|noto → ttf filename).
    std::string hd_font = "FreckleFace-Regular.ttf";
    // Smooth-present tuning (config keys smooth_subframes / smooth_vsync,
    // presentation/render/smooth_config.hpp).  run_game publishes them.
    int smooth_subframes = 0;      // 0 = auto
    bool smooth_vsync_off = false;
    // --window WxH (0 = integer-scaled default), e.g. 1680x720 to simulate
    // ultrawide; the peek margins need --aspect widescreen.
    int window_w = 0, window_h = 0;
    // Persist a (key, value) to play.json; wired by the app layer (which owns
    // config I/O).  No-op if unset.
    std::function<void(const std::string&, const std::string&)> persist;
    // Quicksave file path (app layer derives it from the config dir).  Empty
    // disables the Pause → Save/Load Game actions.
    std::string save_path;
};

int run_game(const GameOptions& opts);

}  // namespace olduvai::presentation
