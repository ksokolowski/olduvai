// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// AdLib/OPL sound-effect renderer.  The original Prehistorik SFX are FM
// synthesis driven by raw OPL register writes (channel 3), NOT digital
// samples.  This renders them through the vendored Nuked-OPL3 core, byte-
// faithful to the walked HISTORIK.EXE pipeline:
//
//   AdLib_KeyOff(ch=3)                              FUN_1fe0_038c
//   AdLib_LoadVoice(mod+car patch, ch=3)            FUN_1fe0_018b
//      → AdLib_CommitVoice → 7 register-group writers   FUN_1fe0_05ed/0635..08b5
//   MDI_ChannelNoteSet(ch=3, note, vel=0x7f)        FUN_1ecd_045a
//      → AdLib_NoteOn → A0+ch / B0+ch (key-on)       FUN_1fe0_02ea / 2c80_0006
//
// The Python reference renders the SAME register stream
// through the SAME Nuked-OPL3 core, so the two PCM outputs match.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olduvai::prepare {
struct AdlibSfxVoices;
}

namespace olduvai::presentation {

// A 13-byte AdLib instrument-patch slot (read from the user's executable's
// 26-byte record at stride 2).  Index meaning (per the walked Finding):
//   0 KSL  1 Mult  2 (unused)  3 AttackRate  4 SustainLevel  5 EG-type
//   6 DecayRate  7 ReleaseRate  8 TL-base  9 AM  10 Vib  11 KSR  12 FB/conn
struct OplSfxVoice {
    int b[13];
};

// One AdLib SFX record: the voice patches read from the user's executable
// plus the authored playback parameters.
struct OplSfxDef {
    const char* id;
    OplSfxVoice modulator;
    OplSfxVoice carrier;
    int modulator_waveform;   // 0..3
    int carrier_waveform;     // 0..3
    int note;                 // caller MIDI note (EXE subtracts 12 internally)
    int velocity;             // 1..127 (clipped to 0x7f)
    int channel;              // OPL melody channel (3 for all current SFX)
    int fb_alg;               // RC0+ch feedback/algorithm byte (DS:0x1bb5)
    int gate_ms;              // key-on duration
    int tail_ms;              // post-key-off release ring
};

// Render an AdLib voice to interleaved-stereo int16 PCM at `sample_rate`.
// Returns gate+tail frames; empty on invalid input.
std::vector<std::int16_t> render_adlib_sfx(const OplSfxVoice& modulator,
                                           const OplSfxVoice& carrier,
                                           int modulator_waveform,
                                           int carrier_waveform,
                                           int note, int velocity, int channel,
                                           int fb_alg, int sample_rate,
                                           int gate_ms, int tail_ms);

// Install the AdLib voice patches read from the user's executable
// (prepare::read_adlib_sfx_voices).  Until this runs, the catalog is empty:
// lookups return nullptr, ids() is empty, renders return no PCM — the
// audio layer then falls back to the VOC sample path.
void install_adlib_sfx_voices(const prepare::AdlibSfxVoices& voices);

// Look up an SFX def by id (e.g. "SFX_HIT"); nullptr if unknown or the
// voice catalog has not been installed yet.
const OplSfxDef* opl_sfx_lookup(const std::string& id);

// All installed AdLib SFX ids (for preloading).
std::vector<std::string> opl_sfx_ids();

// Convenience: render a built-in SFX id directly; empty if unknown.
std::vector<std::int16_t> render_adlib_sfx_by_id(const std::string& id,
                                                 int sample_rate);

}  // namespace olduvai::presentation
