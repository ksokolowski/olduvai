// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Authentic AdLib music driver — port of the reference EXE-faithful OPL
// renderer onto the vendored
// Nuked-OPL3 core.  Transcribed 1:1 so the PCM output byte-matches the
// reference render (same emulator, same integer/double arithmetic, same
// register-write order); the parity harness is tools/opl_music_dump.cpp.

#include "presentation/opl_music.hpp"

#include <cfenv>
#include <cmath>
#include <cstring>

#include "opl3.h"

namespace olduvai::presentation {

namespace {

// OPL2 voice-slot geometry (18 operator slots, 9 channels).
constexpr int kOperatorRegisterBase[18] = {0,  1,  2,  3,  4,  5,  8,  9,  10,
                                           11, 12, 13, 16, 17, 18, 19, 20, 21};
constexpr int kChannelRegisterBase[18] = {0, 1, 2, 0, 1, 2, 3, 4, 5,
                                          3, 4, 5, 6, 7, 8, 6, 7, 8};
constexpr int kChannelSlotFlags[18] = {0, 0, 0, 1, 1, 1, 0, 0, 0,
                                       1, 1, 1, 0, 0, 0, 1, 1, 1};

// Logical channel → (primary slot, secondary slot); -1 = none.  Channels
// 0-5 are melodic operator pairs, 6-10 the rhythm-mode voices.
constexpr int kSlotMap[11][2] = {{0, 3},   {1, 4},   {2, 5},  {6, 9},
                                 {7, 10},  {8, 11},  {12, 15}, {16, -1},
                                 {14, -1}, {17, -1}, {13, -1}};

// Rhythm channel → BD-register key bit (BD = tremolo/vibrato/rhythm gate).
constexpr int kRhythmBits[11] = {0, 0, 0, 0, 0, 0, 0x10, 0x08, 0x04, 0x02, 0x01};

constexpr double kOplClockHz = 49716.0;

// EXE-faithful TL scaling per AdLib_ChangeVolume (FUN_1fe0_0234):
//   tmp = 0x3f - (patch_tl & 0x3f)
//   tl  = 0x3f - ((velocity & 0x7f) * tmp + 0x40) >> 7
// velocity 0x7f → tl == patch_tl (max output); velocity 0 → 0x3f (silent).
int scale_tl_by_velocity(int patch_tl, int velocity) {
    const int tmp = 0x3f - (patch_tl & 0x3f);
    return 0x3f - (((velocity & 0x7f) * tmp + 0x40) >> 7);
}

// frequency → (fnum, block).  Mirrors the reference: Python round() is
// ties-to-even, matched here by nearbyint under FE_TONEAREST (the default
// mode); ldexp scales by the exact power of two.
void frequency_to_fnum_block(double frequency, int& fnum, int& block) {
    for (block = 0; block < 8; ++block) {
        const long v = std::lrint(std::nearbyint(
            std::ldexp(frequency, 20 - block) / kOplClockHz));
        if (v >= 0 && v <= 0x3FF) {
            fnum = static_cast<int>(v);
            return;
        }
    }
    fnum = 0x3FF;
    block = 7;
}

}  // namespace

OplMusicPlayer::OplMusicPlayer(int sample_rate)
    : chip_(std::make_unique<_opl3_chip>()), sample_rate_(sample_rate) {
    std::memset(chip_.get(), 0, sizeof(_opl3_chip));
    OPL3_Reset(chip_.get(), static_cast<uint32_t>(sample_rate_));
}

OplMusicPlayer::~OplMusicPlayer() = default;

void OplMusicPlayer::write_reg(int reg, int val) {
    if (reg_tap) reg_tap(reg, val);
    OPL3_WriteReg(chip_.get(), static_cast<uint16_t>(reg & 0x1FF),
                  static_cast<uint8_t>(val & 0xFF));
}

void OplMusicPlayer::reset_chip() {
    // Mirrors the reference _reset_chip: zero every register, enable
    // waveform select (R01 bit 5), then the BD depth/rhythm state.
    for (int reg = 0; reg < 256; ++reg) write_reg(reg, 0);
    write_reg(0x01, 0x20);
    write_bd_register();
}

bool OplMusicPlayer::open(const std::vector<std::uint8_t>& raw) {
    stream_ = formats::parse_mdi_events(raw);
    cursor_ = 0;
    last_tick_ = 0;
    tempo_us_ = 500000;
    fractional_samples_ = 0.0;
    pending_frames_ = 0;
    tail_done_ = false;
    depth_tremolo_ = true;
    depth_vibrato_ = true;
    rhythm_mode_ = true;
    rhythm_mask_ = 0;
    channels_.fill(ChannelState{});
    b_registers_.fill(0);
    std::memset(chip_.get(), 0, sizeof(_opl3_chip));
    OPL3_Reset(chip_.get(), static_cast<uint32_t>(sample_rate_));
    reset_chip();
    active_ = stream_.valid && !stream_.events.empty();
    return active_;
}

void OplMusicPlayer::stop() {
    if (active_) all_notes_off();
    active_ = false;
}

void OplMusicPlayer::generate(int frames, std::int16_t* out) {
    OPL3_GenerateStream(chip_.get(), out, static_cast<uint32_t>(frames));
}

int OplMusicPlayer::advance_events() {
    for (;;) {
        if (cursor_ >= stream_.events.size()) {
            if (loop_ && !stream_.events.empty()) {
                // EXE loop: the MDI dispatcher resets the stream pointer
                // (DS:0x8834) without touching the chip — gapless, ringing
                // notes carry into the next iteration.
                cursor_ = 0;
                last_tick_ = 0;
                tempo_us_ = 500000;
                fractional_samples_ = 0.0;
                continue;
            }
            if (!tail_done_) {
                // One-shot: all-notes-off + 350 ms so release envelopes
                // finish (reference render_to_wav loop=False).
                tail_done_ = true;
                all_notes_off();
                return static_cast<int>(sample_rate_ * 0.35);
            }
            active_ = false;
            return 0;
        }
        const formats::MdiStreamEvent& e = stream_.events[cursor_];
        const std::uint32_t delta =
            e.tick >= last_tick_ ? e.tick - last_tick_ : 0;
        if (delta > 0 && stream_.division > 0) {
            // Mirrors _render_delta: one double conversion per event delta,
            // truncating to whole frames and carrying the fraction.
            const double seconds =
                static_cast<double>(static_cast<std::uint64_t>(delta) *
                                    tempo_us_) /
                (stream_.division * 1000000.0);
            const double sample_count =
                seconds * sample_rate_ + fractional_samples_;
            const int whole = static_cast<int>(sample_count);
            fractional_samples_ = sample_count - whole;
            last_tick_ = e.tick;
            if (whole > 0) return whole;
            continue;   // sub-frame delta — handle the event next pass
        }
        last_tick_ = e.tick;
        handle_event(e);
        ++cursor_;
    }
}

int OplMusicPlayer::render(int frames, std::int16_t* out) {
    int filled = 0;
    while (filled < frames && active_) {
        if (pending_frames_ == 0) {
            pending_frames_ = advance_events();
            continue;
        }
        const int n = std::min(frames - filled, pending_frames_);
        generate(n, out + static_cast<std::ptrdiff_t>(filled) * 2);
        filled += n;
        pending_frames_ -= n;
    }
    for (int i = filled * 2; i < frames * 2; ++i) out[i] = 0;
    return filled;
}

void OplMusicPlayer::handle_event(const formats::MdiStreamEvent& e) {
    using Kind = formats::MdiStreamEvent::Kind;
    if (e.kind == Kind::Tempo) {
        tempo_us_ = e.tempo_us;
        return;
    }
    if (e.kind == Kind::Seq) {
        handle_seq_event(e.seq);
        return;
    }
    const int event_type = e.status & 0xF0;
    const int channel = e.status & 0x0F;
    if (channel >= static_cast<int>(channels_.size())) return;

    switch (event_type) {
        case 0x90:
            if (e.data2 != 0) {
                note_on(channel, e.data1, e.data2);
            } else {
                note_off(channel);
            }
            break;
        case 0x80:
            note_off(channel);
            break;
        case 0xA0:
            // Poly aftertouch — EXE handler 1ecd:06e3 routes the per-note
            // pressure through AdLib_ChangeVolume as the velocity arg.
            apply_channel_volume(channel, e.data2);
            break;
        case 0xD0:
            // Channel aftertouch — EXE handler 1ecd:0727; drives dynamic
            // rhythm attenuation (e.g. silence-all-rhythm in rik1).
            apply_channel_volume(channel, e.data1);
            break;
        case 0xE0:
            channels_[static_cast<std::size_t>(channel)].pitch_bend =
                ((e.data2 & 0x7F) << 7) | (e.data1 & 0x7F);
            if (channel < 6) {
                refresh_melodic_pitch(channel);
            } else if (channel == 6) {
                refresh_rhythm_pitch(channel);
            }
            break;
        default:
            break;
    }
}

void OplMusicPlayer::handle_seq_event(const formats::MdiSeqEvent& seq) {
    if (seq.message_type == 0x0002) {
        depth_tremolo_ = seq.value != 0;
        write_bd_register();
    } else if (seq.message_type == 0x0003) {
        depth_vibrato_ = seq.value != 0;
        write_bd_register();
    } else if (seq.message_type == 0x0001 && seq.has_timbre) {
        apply_channel_timbre(seq);
    }
}

void OplMusicPlayer::apply_channel_timbre(const formats::MdiSeqEvent& seq) {
    if (seq.channel < 0 || seq.channel >= 11) return;
    const int primary_slot = kSlotMap[seq.channel][0];
    const int secondary_slot = kSlotMap[seq.channel][1];

    apply_slot_state(primary_slot, seq.primary, seq.channel);
    if (secondary_slot >= 0) {
        apply_slot_state(secondary_slot, seq.secondary, seq.channel);
    }
    if (kChannelSlotFlags[primary_slot] == 0) {
        const int channel_index = kChannelRegisterBase[primary_slot];
        const int feedback_value = (seq.primary.feedback & 0x07) << 1;
        const int connection_value = seq.primary.connection_additive ? 1 : 0;
        write_reg(0xC0 + channel_index, feedback_value | connection_value);
    }
}

void OplMusicPlayer::apply_slot_state(
    int slot, const formats::MdiOplOperatorState& st, int logical_channel) {
    const int op = kOperatorRegisterBase[slot];
    const int reg_20 = (st.amplitude_modulation ? 0x80 : 0) |
                       (st.vibrato ? 0x40 : 0) |
                       (st.sustain_enabled ? 0x20 : 0) |
                       (st.key_scale_rate ? 0x10 : 0) |
                       (st.frequency_multiplier & 0x0F);
    // Cache TL base + KSL so note-ons can rewrite R40 velocity-scaled.
    if (logical_channel >= 0 && logical_channel < 11) {
        ChannelState& ch = channels_[static_cast<std::size_t>(logical_channel)];
        const int idx = slot == kSlotMap[logical_channel][0] ? 0 : 1;
        ch.slot_tl_base[static_cast<std::size_t>(idx)] = st.total_level & 0x3F;
        ch.slot_ksl[static_cast<std::size_t>(idx)] = st.key_scale_level & 0x03;
    }
    const int reg_40 = ((st.key_scale_level & 0x03) << 6) |
                       (st.total_level & 0x3F);
    const int reg_60 = ((st.attack_rate & 0x0F) << 4) | (st.decay_rate & 0x0F);
    const int reg_80 = ((st.sustain_level & 0x0F) << 4) |
                       (st.release_rate & 0x0F);
    const int reg_e0 = st.waveform & 0x03;

    write_reg(0x20 + op, reg_20);
    write_reg(0x40 + op, reg_40);
    write_reg(0x60 + op, reg_60);
    write_reg(0x80 + op, reg_80);
    write_reg(0xE0 + op, reg_e0);
}

void OplMusicPlayer::apply_channel_volume(int logical_channel, int velocity) {
    // Mirrors AdLib_ChangeVolume (FUN_1fe0_0234) via MDI_ChannelNoteSet
    // (FUN_1ecd_045a): only rewrite R40 when the velocity changed (the EXE
    // caches it at DS:0x88c0+ch*2).  Applies to BOTH operator slots — the
    // carrier-only gating at FUN_1fe0_0635:0x0685 is deferred until the
    // reference lands it too (its Finding notes a parser-side patch_tl
    // mismatch that must be resolved first; parity means matching the
    // reference as-is).
    if (logical_channel < 0 || logical_channel >= 11) return;
    ChannelState& ch = channels_[static_cast<std::size_t>(logical_channel)];
    if (ch.cached_velocity == velocity) return;
    ch.cached_velocity = velocity;
    for (int idx = 0; idx < 2; ++idx) {
        const int slot = kSlotMap[logical_channel][idx];
        if (slot < 0) continue;
        const int patch_tl = ch.slot_tl_base[static_cast<std::size_t>(idx)];
        if (patch_tl < 0) continue;
        const int ksl = ch.slot_ksl[static_cast<std::size_t>(idx)];
        const int scaled_tl = scale_tl_by_velocity(patch_tl, velocity);
        write_reg(0x40 + kOperatorRegisterBase[slot],
                  ((ksl & 0x03) << 6) | (scaled_tl & 0x3F));
    }
}

void OplMusicPlayer::note_on(int logical_channel, int midi_note,
                             int velocity) {
    ChannelState& ch = channels_[static_cast<std::size_t>(logical_channel)];
    ch.midi_note = midi_note;
    apply_channel_volume(logical_channel, velocity);
    if (logical_channel < 6) {
        refresh_melodic_pitch(logical_channel);
        return;
    }
    refresh_rhythm_pitch(logical_channel);
    rhythm_mask_ |= kRhythmBits[logical_channel];
    write_bd_register();
}

void OplMusicPlayer::note_off(int logical_channel) {
    channels_[static_cast<std::size_t>(logical_channel)].midi_note = -1;
    if (logical_channel >= 6) {
        rhythm_mask_ &= ~kRhythmBits[logical_channel];
        write_bd_register();
        return;
    }
    int& b = b_registers_[static_cast<std::size_t>(logical_channel)];
    b &= ~0x20;
    write_reg(0xB0 + logical_channel, b);
}

void OplMusicPlayer::refresh_melodic_pitch(int logical_channel) {
    const ChannelState& ch =
        channels_[static_cast<std::size_t>(logical_channel)];
    if (ch.midi_note < 0) return;
    const double bend =
        (ch.pitch_bend - 0x2000) / static_cast<double>(0x2000) * 2.0;
    set_opl_pitch(logical_channel, ch.midi_note + bend, true);
}

void OplMusicPlayer::refresh_rhythm_pitch(int logical_channel) {
    const ChannelState& ch =
        channels_[static_cast<std::size_t>(logical_channel)];
    if (ch.midi_note < 0) return;
    const double bend =
        (ch.pitch_bend - 0x2000) / static_cast<double>(0x2000) * 2.0;
    const double base_note = ch.midi_note + bend;
    if (logical_channel == 6) {
        set_opl_pitch(6, base_note, false);
    } else if (logical_channel == 8) {
        set_opl_pitch(8, base_note, false);
        set_opl_pitch(7, base_note + 7.0, false);
    }
}

void OplMusicPlayer::set_opl_pitch(int opl_channel, double midi_note,
                                   bool key_on) {
    const double frequency =
        440.0 * std::pow(2.0, (midi_note - 69.0) / 12.0);
    int fnum = 0, block = 0;
    frequency_to_fnum_block(frequency, fnum, block);
    write_reg(0xA0 + opl_channel, fnum & 0xFF);
    int b_reg = ((block & 0x07) << 2) | ((fnum >> 8) & 0x03);
    if (key_on) b_reg |= 0x20;
    b_registers_[static_cast<std::size_t>(opl_channel)] = b_reg;
    write_reg(0xB0 + opl_channel, b_reg);
}

void OplMusicPlayer::write_bd_register() {
    const int value = (depth_tremolo_ ? 0x80 : 0) |
                      (depth_vibrato_ ? 0x40 : 0) |
                      (rhythm_mode_ ? 0x20 : 0) | rhythm_mask_;
    write_reg(0xBD, value);
}

void OplMusicPlayer::all_notes_off() {
    for (int channel = 0; channel < 6; ++channel) {
        b_registers_[static_cast<std::size_t>(channel)] = 0;
        write_reg(0xB0 + channel, 0);
    }
    rhythm_mask_ = 0;
    write_bd_register();
}

}  // namespace olduvai::presentation
