// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// AdLib/OPL sound effects: the original SFX are FM synthesis (raw OPL register
// writes on channel 3), not samples.  Rendered through the vendored
// Nuked-OPL3 core, following the executable's pipeline:
//   AdLib_KeyOff(ch=3)                              FUN_1fe0_038c
//   AdLib_LoadVoice(mod+car patch, ch=3)            FUN_1fe0_018b
//      -> AdLib_CommitVoice -> 7 register writers   FUN_1fe0_05ed/0635..08b5
//   MDI_ChannelNoteSet(ch=3, note, vel=0x7f)        FUN_1ecd_045a
//      -> AdLib_NoteOn -> A0+ch / B0+ch (key-on)    FUN_1fe0_02ea / 2c80_0006
// The reference feeds the same register stream to the same core, so the PCM
// matches.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olduvai::prepare {
struct AdlibSfxVoices;
}

namespace olduvai::presentation {

// A 13-byte patch slot (from the executable's 26-byte record, stride 2):
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

// Render an SFX record to interleaved-stereo int16 at `sample_rate`: gate +
// tail frames; empty on invalid input.
std::vector<std::int16_t> render_adlib_sfx(const OplSfxDef& def,
                                           int sample_rate);

// Install the patches read from the user's executable.  Until then the catalog
// is empty (lookups null, no PCM) and the audio layer uses the VOC samples.
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
