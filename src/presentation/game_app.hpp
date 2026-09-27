// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Playable game shell — M4 work-in-progress.  Currently: L1, single-screen
// scope (screen transitions and level flow land next).

#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "presentation/enhance_flags.hpp"

namespace olduvai::presentation {

struct GameOptions {
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
    // Enhanced mode (opt-in).  `enhanced`: the HD substrate is on; `enhance`:
    // the individual effects (--enhanced sets them all).
    bool enhanced = false;
    EnhanceFlags enhance;
    std::string hd_profile;   // "" native | "mmpx"
    int render_scale = 4;     // 2 or 4; default 4 → omniscale 1280x800
    std::string aspect = "keep"; // keep | 4:3 | stretch
    // auto | mt32-builtin | gm-builtin | opl | none | host-midi (mt32 alias)
    // | gm-host (host MIDI with GM translation, e.g. Windows GS synth)
    std::string music_device = "auto";
    std::string midi_port;                // host-midi: MIDI OUT port name ("" = default)
    std::string rom_dir;                  // MT-32 ROM override
    std::string mt32_model;               // auto|cm32l|mt32
    std::string soundfont;                // GM SoundFont override
    // "auto" pairs with the music device (mt32 -> mt32-sfx, gm -> gm-sfx,
    // opl/none -> sb-dac).  Explicit: opl | sb-dac | mt32-sfx | gm-sfx | midi.
    std::string sfx_backend = "auto";
    std::string replay;       // inputs.jsonl — scripted input replay
    std::string trace;        // out.jsonl — frame-state trace
    std::string record_inputs;  // out.jsonl — record live inputs (replay schema)
    bool cheats = false;      // --cheats: number keys 1-6 grant power-ups (test aid)
    bool god = false;         // --god: 99 lives, 999 energy, no death (debug).
                              // --autofire: holding attack emulates paced
                              // mashing (the input path inserts latch-clearing
                              // releases); systems unchanged.  off | slow |
                              // medium | fast (anything else = off).  Config
                              // key "autofire"; ignored under --replay.
    std::string autofire = "off";
    // Debug overlays (render aids, not reference-parity); off = untouched
    // rendering.
    bool debug_collision = false;  // tint solid collision cells
    bool debug_entities = false;   // box every active+visible entity
    bool debug_perf = false;       // FPS + frame-time text corner
    // ── Tuning knobs (mirror reference op play; default = byte-faithful).
    bool vsync = false;            // --vsync: SDL_RENDERER_PRESENTVSYNC
    // Gamepad mapping (play.json pad_*, SDL button names), parsed by
    // presentation/gamepad; strings keep this header SDL-free.
    std::string pad_jump = "a", pad_attack = "x", pad_pause = "start",
                pad_confirm = "a", pad_back = "b";
    int pad_deadzone = 8000;
    // --vga-scan (classic): re-present the held frame every refresh between
    // logic ticks (VGA scanning VRAM at 70 Hz).  Same pixels; implies vsync.
    bool vga_scan = false;
    bool fullscreen = false;       // -f/--fullscreen: start desktop-fullscreen
    // --display-mode gpu|cpu: gpu = SDL_RENDERER_ACCELERATED (GPU scaling,
    // default), cpu = SDL_RENDERER_SOFTWARE (CPU window scaling).
    std::string display_mode = "gpu";
    int audio_rate = 0;            // --audio-rate Hz (0 = device default/auto)
    int audio_buffer = 0;          // --audio-buffer frames (0 = 2048 default)
    // --transitions smooth|classic: classic forces smooth_motion off (a
    // convenience override of the enhance flag).  smooth = leave as-is.
    std::string transitions = "smooth";
    // --hd-font: vector face file for HD text (freckle|noto → ttf filename).
    std::string hd_font = "FreckleFace-Regular.ttf";
    // --banner-fx: GET READY / NOT ENOUGH FOOD / OLDUVAI title effect: caveman
    // | fire | rainbow | gold | pulse.  OLDUVAI_BANNER_FX overrides.
    std::string banner_fx = "caveman";
    // Smooth-present tuning (config keys smooth_subframes / smooth_vsync,
    // presentation/render/smooth_config.hpp).  run_game publishes them.
    int smooth_subframes = 0;      // 0 = auto
    bool smooth_vsync_off = false;
    // The session's profile family (presentation/menu/profile_table.hpp):
    // decides what the Options menu's Classic/Enhanced preset applies.
    std::string profile_family = "desktop";
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
