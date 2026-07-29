// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Authentic AdLib music driver — C++ port of the reference EXE-faithful OPL
// renderer, streaming through the
// vendored Nuked-OPL3 core.  Music MUST use the same emulator as the OPL SFX
// path (opl_sfx.cpp) so the music-vs-SFX volume balance matches what the
// EXE/DOSBox-X produces — DBOPL pumps music to clipping where Nuked sits at
// ~70% peak.
//
// The driver consumes the RAW music container (formats::parse_mdi_events) so
// the FF 7F voice patches reach the chip — the GM converter strips them.
// Key EXE mirrors (evidence cited at the implementation sites):
//   * velocity→TL scaling        AdLib_ChangeVolume   FUN_1fe0_0234
//   * note-set semantics         MDI_ChannelNoteSet   FUN_1ecd_045a
//   * carrier/modulator gating   flag table           DS:0x1ba3
//   * loop = stream-pointer reset without chip reset (gapless, ringing notes
//     carry into the next iteration — MDI dispatcher resets to DS:0x8834).

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "formats/mdi.hpp"

struct _opl3_chip;   // vendored Nuked-OPL3 (third_party/nuked_opl3/opl3.h)

namespace olduvai::presentation {

class OplMusicPlayer {
public:
    explicit OplMusicPlayer(int sample_rate);
    ~OplMusicPlayer();
    OplMusicPlayer(const OplMusicPlayer&) = delete;
    OplMusicPlayer& operator=(const OplMusicPlayer&) = delete;

    // Parse + start the track from the beginning (chip fully reset).
    // Returns false (and goes inactive) on a malformed container.
    bool open(const std::vector<std::uint8_t>& raw);
    void set_loop(bool on) { loop_ = on; }
    // Stop playback and silence the chip.
    void stop();
    bool active() const { return active_; }

    // Render exactly `frames` stereo int16 frames (interleaved, 2*frames
    // shorts).  Zero-fills past the end of a non-looping track; returns the
    // number of frames actually generated before the fill (== frames while
    // the track plays) so one-shot renders can trim to the exact length.
    int render(int frames, std::int16_t* out);

    // Test observer: sees every OPL register write (reg, value).
    std::function<void(int, int)> reg_tap;

private:
    void write_reg(int reg, int val);
    void reset_chip();
    void generate(int frames, std::int16_t* out);
    void handle_event(const formats::MdiStreamEvent& e);
    void handle_seq_event(const formats::MdiSeqEvent& seq);
    void apply_channel_timbre(const formats::MdiSeqEvent& seq);
    void apply_slot_state(int slot, const formats::MdiOplOperatorState& st,
                          int logical_channel);
    void apply_channel_volume(int logical_channel, int velocity);
    void note_on(int logical_channel, int midi_note, int velocity);
    void note_off(int logical_channel);
    void refresh_melodic_pitch(int logical_channel);
    void refresh_rhythm_pitch(int logical_channel);
    void set_opl_pitch(int opl_channel, double midi_note, bool key_on);
    void write_bd_register();
    void all_notes_off();
    // Advance to the next pending event gap; returns frames until the next
    // event (0 when the stream is exhausted and not looping).
    int advance_events();

    struct ChannelState {
        int midi_note = -1;                 // -1 = none
        int pitch_bend = 0x2000;
        int cached_velocity = -1;           // DS:0x88c0+ch*2 mirror
        // Patch TL/KSL cached at instrument-change time (primary, secondary)
        // so note-ons rewrite R40 with velocity-scaled TL.
        std::array<int, 2> slot_tl_base{{-1, -1}};
        std::array<int, 2> slot_ksl{{0, 0}};
    };

    std::unique_ptr<_opl3_chip> chip_;
    int sample_rate_;
    formats::MdiEventStream stream_;
    std::size_t cursor_ = 0;
    std::uint32_t last_tick_ = 0;
    std::uint32_t tempo_us_ = 500000;
    double fractional_samples_ = 0.0;
    int pending_frames_ = 0;   // frames to render before the event at cursor_
    bool tail_done_ = false;   // one-shot release tail already scheduled
    bool loop_ = false;
    bool active_ = false;
    bool depth_tremolo_ = true;
    bool depth_vibrato_ = true;
    bool rhythm_mode_ = true;
    int rhythm_mask_ = 0;
    std::array<ChannelState, 11> channels_{};
    std::array<int, 9> b_registers_{};
};

}  // namespace olduvai::presentation
