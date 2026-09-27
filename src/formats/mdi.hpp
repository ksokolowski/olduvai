// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Music container -> standard MIDI.  The container is MThd/MTrk MIDI with two
// quirks the original driver resolves at play time:
//   * Note Off is `0x8X 0x00 vel` (note implicit): the driver keeps a
//     per-channel last note (sentinel 0x80) and releases a held note when a
//     new Note On arrives on that channel.
//   * FF 7F sequencer-specific metas carry OPL timbres: stripped for MIDI,
//     with per-track Roland program presets injected up front (volume 7=127).
// Strict mode drops runtime CC / aftertouch / channel pressure / pitch bend:
// the original sends only Note On/Off and Program Change to the Roland.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace olduvai::formats {

// Per-track Roland program presets (authored catalog; channel → program).
const std::map<int, int>* roland_program_map(int track_id);

// Music name (lowercase) → track id for the preset table.
int mdi_track_id(const std::string& lower_name);

// Raw container -> standard MIDI.  gm_translate maps Roland programs through
// the approximate MT-32 -> GM table (SoundFont targets).
std::vector<std::uint8_t> build_gm_midi(
    const std::vector<std::uint8_t>& raw, int track_id,
    bool mt32_strict = false, bool gm_translate = false);

// ---- OPL event stream (the AdLib branch) ----
// The FF 7F events the GM converter strips carry the OPL voice patches:
//   bytes 0..2   manufacturer id
//   bytes 3..4   message type (big-endian)
//   type 0x0001, len 34: byte 5 = channel; then two 13-byte operator blocks
//     (primary, secondary) + waveform bytes at offsets 26/27
//   type 0x0002/0x0003, len 6: byte 5 = tremolo / vibrato depth

// One OPL operator voice block from a type-0x0001 timbre event.
struct MdiOplOperatorState {
    std::uint8_t key_scale_level = 0;       // byte 0  & 0x03
    std::uint8_t frequency_multiplier = 0;  // byte 1  & 0x0F
    std::uint8_t feedback = 0;              // byte 2  & 0x07 (channel-level)
    std::uint8_t attack_rate = 0;           // byte 3  & 0x0F
    std::uint8_t sustain_level = 0;         // byte 4  & 0x0F
    bool sustain_enabled = false;           // byte 5  != 0
    std::uint8_t decay_rate = 0;            // byte 6  & 0x0F
    std::uint8_t release_rate = 0;          // byte 7  & 0x0F
    std::uint8_t total_level = 0;           // byte 8  & 0x3F
    bool amplitude_modulation = false;      // byte 9  != 0
    bool vibrato = false;                   // byte 10 != 0
    bool key_scale_rate = false;            // byte 11 != 0
    bool connection_additive = false;       // byte 12 == 0
    std::uint8_t waveform = 0;              // waveform byte & 0x03
};

// A decoded FF 7F sequencer-specific event.
struct MdiSeqEvent {
    int message_type = -1;   // 0x0001 timbre / 0x0002 tremolo / 0x0003 vibrato
    int channel = -1;        // type 0x0001 only
    int value = 0;           // types 0x0002/0x0003
    bool has_timbre = false;
    MdiOplOperatorState primary{};
    MdiOplOperatorState secondary{};
};

// One event of the raw (unconverted) MDI stream, absolute-tick timed.
struct MdiStreamEvent {
    enum class Kind : std::uint8_t { Channel, Tempo, Seq };
    std::uint32_t tick = 0;
    Kind kind = Kind::Channel;
    std::uint8_t status = 0;   // Channel: full status byte (type | channel)
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
    std::uint32_t tempo_us = 0;   // Tempo (meta 0x51)
    MdiSeqEvent seq{};            // Seq (meta 0x7F)
};

struct MdiEventStream {
    bool valid = false;
    int division = 0;
    std::vector<MdiStreamEvent> events;
};

// Parse the container into the OPL event stream: channel events on raw
// channels (no MT-32 remap), tempo metas, decoded FF 7F timbres; other metas
// and sysex dropped.  valid=false on a malformed container.
MdiEventStream parse_mdi_events(const std::vector<std::uint8_t>& raw);

}  // namespace olduvai::formats
