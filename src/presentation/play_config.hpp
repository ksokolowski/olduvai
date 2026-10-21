// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The saved settings that reach a session unchanged: each field is one
// play.json key.  app::PlaySettings resolves them (defaults < profiles <
// play.json < CLI) and GameOptions runs with them; both derive from this, so a
// new pass-through setting is one field here and one row in the resolve table
// (app/options_resolve.cpp).  Settings that change form on the way (hd_font,
// the smooth-present keys, vga_scan) stay in each struct.
//
// Pure and header-only: no SDL.
#pragma once

#include <string>

namespace olduvai::presentation {

struct PlayConfig {
    bool enhanced = false;
    std::string hd_profile;   // "" native | "mmpx" | ...
    int render_scale = 4;     // 2 to 4; default 4 -> omniscale 1280x800
    std::string aspect = "keep";   // keep | 4:3 | stretch | widescreen
    // nearest | sharp (sharp bilinear): a window that is not a whole
    // multiple of the picture (render/output_filter.hpp).
    std::string output_filter = "nearest";
    // auto | mt32-builtin | gm-builtin | opl | none | host-midi (mt32 alias)
    // | gm-host (host MIDI with GM translation, e.g. Windows GS synth)
    std::string music_device = "auto";
    // Whether "auto" may pick a GM SoundFont (FluidSynth) after MT-32: the
    // handheld profiles say no, so auto there is MT-32, then the Sound
    // Blaster (OPL).  An explicit gm-builtin is unaffected.
    bool music_auto_gm = true;
    std::string rom_dir;      // MT-32 ROM override
    std::string mt32_model;   // "" auto | cm32l | mt32
    std::string soundfont;    // GM SoundFont override
    // "auto" pairs with the music device (mt32 -> mt32-sfx, gm -> gm-sfx,
    // opl/none -> sb-dac).  Explicit: opl | sb-dac | mt32-sfx | gm-sfx | midi.
    std::string sfx_backend = "auto";
    // Holding attack emulates paced mashing (the input path inserts
    // latch-clearing releases); systems unchanged.  off | slow | medium |
    // fast (anything else = off).  Ignored under --replay.
    std::string autofire = "off";
    // Gamepad mapping (SDL button names, up to two per action), parsed by
    // presentation/gamepad; strings keep this header SDL-free.
    std::string pad_jump = "a", pad_attack = "x", pad_pause = "start",
                pad_confirm = "a", pad_back = "b";
    int pad_deadzone = 8000;
    // Keyboard play bindings (SDL key names, "" = the action's defaults),
    // read by presentation/input/actions.
    std::string key_left, key_right, key_up, key_down, key_attack, key_pause,
        key_quit;
    // gpu = SDL_RENDERER_ACCELERATED (GPU scaling, default), cpu =
    // SDL_RENDERER_SOFTWARE (CPU window scaling).
    std::string display_mode = "gpu";
    int audio_rate = 0;       // Hz (0 = device default / auto)
    int audio_buffer = 0;     // frames (0 = 2048 default)
    // smooth | classic: classic forces smooth_motion off (a convenience
    // override of the enhance flag).
    std::string transitions = "smooth";
    // GET READY / NOT ENOUGH FOOD / OLDUVAI title effect: caveman | fire |
    // rainbow | gold | pulse.  OLDUVAI_BANNER_FX overrides.
    std::string banner_fx = "caveman";
    // The session's profile family (presentation/menu/profile_table.hpp):
    // decides what the Options menu's Classic/Enhanced preset applies.
    std::string profile_family = "desktop";
};

}  // namespace olduvai::presentation
