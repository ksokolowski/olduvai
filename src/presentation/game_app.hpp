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
    // Sequence position, mirroring the EXE top-level sequencer FUN_2bd7_04be:
    // 0 = attract (intro cards + title + main menu), 1-7 = play levels
    // (display numbering), 8 = win ending.  Default 1 = straight into L1 so
    // direct constructions (tests/tools, headless one-shots) stay
    // deterministic from gameplay frame 0; the CLI maps "no --level" to 0.
    int level = 1;
    // --start-screen N: DEBUG — bind a surface screen index other than 0 at
    // level entry (e.g. the last screen, for widescreen-edge validation that is
    // otherwise slow to reach in-game).  Clamped to the level's screen count;
    // suppresses the GET READY banner.  0 = normal level start.
    int start_screen = 0;
    int frames = -1;          // exit after N frames (headless verification)
    std::string screenshot;   // save frame N (PNG by extension, else BMP) and exit
    int screenshot_frame = 1;
    // Enhanced mode (opt-in; default rendering stays native 320x200).
    // `enhanced` = the HD-render substrate is active (any enhanced feature
    // requested, or an explicit --hd-profile); `enhance` selects which
    // individual effects are on.  --enhanced sets every flag → all-on.
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
    std::string soundfont;                // GM SoundFont override
    // auto pairs to the music device (mt32→mt32-sfx, gm→gm-sfx, opl/none→
    // sb-dac), mirroring Python's _resolve_sfx_backend.  Explicit:
    // opl (AdLib FM) | sb-dac | mt32-sfx | gm-sfx | midi.
    std::string sfx_backend = "auto";
    std::string replay;       // inputs.jsonl — scripted input replay
    std::string trace;        // out.jsonl — frame-state trace
    std::string record_inputs;  // out.jsonl — record live inputs (replay schema)
    bool cheats = false;      // --cheats: number keys 1-6 grant power-ups (test aid)
    bool god = false;         // --god: 99 lives, 999 energy, no death (debug).
                              // Mirrors Python op play --god.
                              // Gated off during --replay like --cheats.
    // --autofire: hold-to-swing assist.  The live-input path synthesizes
    // latch-clearing releases (presentation/autofire.hpp) so holding attack
    // emulates paced mashing; systems stay byte-faithful.  Tokens:
    // off|slow|medium|fast (anything else = off; no bool-era compat).
    // Persisted config key "autofire"; ignored during --replay.
    std::string autofire = "off";
    // Dev/debug visualization overlays (NOT byte-parity with the Python
    // reference — useful render-path aids only).  Each gated so default
    // rendering is untouched when off.
    bool debug_collision = false;  // tint solid collision cells
    bool debug_entities = false;   // box every active+visible entity
    bool debug_perf = false;       // FPS + frame-time text corner
    // ── Tuning knobs (mirror reference op play; default = byte-faithful).
    bool vsync = false;            // --vsync: SDL_RENDERER_PRESENTVSYNC
    // Gamepad button mapping (play.json pad_* keys; SDL button names —
    // "a", "b", "x", "y", "start", "back", "leftshoulder", ...).  Parsed
    // by presentation/gamepad at run start; kept as strings here so this
    // header stays SDL-free.
    std::string pad_jump = "a", pad_attack = "x", pad_pause = "start",
                pad_confirm = "a", pad_back = "b";
    int pad_deadzone = 8000;
    // --vga-scan (classic-only): between 18.2 Hz logic ticks, re-present the
    // held frame every display refresh — the software twin of VGA hardware
    // scanning VRAM at 70 Hz.  Pixel-identical frames; only the presentation
    // cadence changes.  Implies vsync at window creation.  Candidate default
    // for the dos profile after playtest (owner 2026-07-04).
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
    // --banner-fx: colour effect for the enhanced GET READY / NOT ENOUGH FOOD
    // banners (and the OLDUVAI title): caveman | fire | rainbow | gold | pulse.
    // The OLDUVAI_BANNER_FX env var overrides this at runtime when set.
    std::string banner_fx = "caveman";
    // --window WxH: force the window pixel size (0 = integer-scaled default).
    // E.g. 1680x720 ≈ 21:9 to simulate an ultrawide widescreen viewport on a
    // non-ultrawide panel.  Needs --aspect widescreen for the peek margins.
    int window_w = 0, window_h = 0;
    // Persist a (config-key, value) pair to play.json — wired by the app layer
    // (which owns config I/O) so the in-game Options menu can save settings
    // without the presentation lib depending on app/config.  No-op if unset.
    std::function<void(const std::string&, const std::string&)> persist;
    // Quicksave file path (app layer derives it from the config dir).  Empty
    // disables the Pause → Save/Load Game actions.
    std::string save_path;
};

int run_game(const GameOptions& opts);

}  // namespace olduvai::presentation
