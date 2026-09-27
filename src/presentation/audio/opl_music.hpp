// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// AdLib music driver (EXE-faithful OPL) on the vendored Nuked-OPL3 core, the
// same emulator as the OPL SFX so the music/SFX balance matches the EXE (DBOPL
// drives music to clipping).  Consumes the raw music container
// (parse_mdi_events), so the FF 7F voice patches reach the chip.  EXE mirrors:
//   * velocity -> TL scaling       AdLib_ChangeVolume   FUN_1fe0_0234
//   * note-set semantics           MDI_ChannelNoteSet   FUN_1ecd_045a
//   * carrier/modulator gating     flag table           DS:0x1ba3
//   * loop = stream pointer reset (DS:0x8834) without a chip reset: gapless,
//     ringing notes carry into the next pass.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "formats/mdi.hpp"

// Nuked-OPL3's struct tag behind its `opl3_chip` typedef, forward-declared so
// the opaque pointer is the vendor's type.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
struct _opl3_chip;

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

    // Render exactly `frames` stereo int16 frames.  Zero-fills past the end of
    // a non-looping track; returns the frames generated before the fill.
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
