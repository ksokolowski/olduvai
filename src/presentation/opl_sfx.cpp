// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/opl_sfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "prepare/exe_tables.hpp"

extern "C" {
#include "opl3.h"
}

namespace olduvai::presentation {

namespace {

// DS:0x1b69 — melody-mode (modulator, carrier) voice_idx pair by channel.
// ch 3 (the SFX channel) → (6, 9).  FUN_1fe0_018b.
constexpr int kVoiceIdxByChannel[9][2] = {
    {0x00, 0x03}, {0x01, 0x04}, {0x02, 0x05},
    {0x06, 0x09},                                   // ch 3 ← SFX
    {0x07, 0x0a}, {0x08, 0x0b},
    {0x0c, 0x0f}, {0x0d, 0x10}, {0x0e, 0x11},
};

// DS:0x1b91 — OPL operator offset by voice_idx (melody slots 0..0x15).
constexpr int kOpOffsetByVoiceIdx[18] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
};

// DS:0x82c6 row-0 entry-0 — OPL fnum for chromatic step 0 (C), no pitch bend.
constexpr int kFnumC = 0x02b2;

// (fnum, block) per FUN_2c80_0006 with pitch_bend = 0x2000 (neutral).
// adj_note = caller note - 12 (FUN_1fe0_02ea:02f5).
void note_to_fnum_block(int adj_note, int& fnum, int& block) {
    const int block_step = adj_note % 12;             // DS:0x84a6 = note % 12
    if (block_step == 0) {
        fnum = kFnumC;
    } else {
        // Standard chromatic progression from the EXE row-0 base; matches the
        // raw DS:0x82c6 table within +-2 fnum units for rows 1..5.
        fnum = static_cast<int>(std::lround(
            kFnumC * std::pow(2.0, block_step / 12.0)));
    }
    block = adj_note / 12 - 1;                          // DS:0x8446 table, then block-1
    if (block < 0) {
        block = 0;
        fnum >>= 1;                                     // EXE's block+1 / fnum-halving underflow path
    }
}

// One operator's (R20, R40, R60, R80, RE0) bit-packs.  FUN_1fe0_0635..08b5.
struct OpRegs { int r20, r40, r60, r80, re0; };
OpRegs operator_registers(const OplSfxVoice& v, int waveform, int chan_vol) {
    OpRegs r;
    // R20+op: AM | Vib | EG | KSR | Multiplier
    r.r20 = (v.b[9]  ? 0x80 : 0) | (v.b[10] ? 0x40 : 0) |
            (v.b[5]  ? 0x20 : 0) | (v.b[11] ? 0x10 : 0) | (v.b[1] & 0x0f);
    // R40+op: KSL | TL.  tl = 0x3f - ((chan_vol * (0x3f - TLbase) + 0x40) >> 7)
    const int tmp = 0x3f - (v.b[8] & 0x3f);
    const int tl = 0x3f - (((chan_vol & 0x7f) * tmp + 0x40) >> 7);
    r.r40 = ((v.b[0] & 0x03) << 6) | (tl & 0x3f);
    r.r60 = ((v.b[3] & 0x0f) << 4) | (v.b[6] & 0x0f);   // AttackRate | DecayRate
    r.r80 = ((v.b[4] & 0x0f) << 4) | (v.b[7] & 0x0f);   // SustainLevel | Release
    r.re0 = waveform & 0x03;
    return r;
}

// Authored per-SFX playback parameters — single constants with citations
// (note/velocity/channel from the FUN_1ecd_045a call sites, fb from
// DS:0x1bb5, gate/tail are engine envelope times).  The voice PATCHES and
// waveforms are game data: they are read from the user's executable at
// startup (prepare::read_adlib_sfx_voices) and installed here via
// install_adlib_sfx_voices() — the engine ships none of the patch bytes.
struct SfxParams {
    const char* id;
    int note, velocity, channel, fb_alg, gate_ms, tail_ms;
};
constexpr SfxParams kParams[] = {
    {"SFX_HIT", 24, 127, 3, 0, 800, 400},
    {"SFX_JUMP_APEX", 36, 127, 3, 6, 2500, 200},
    {"SFX_GENERIC", 36, 127, 3, 0, 1500, 800},
};

std::vector<OplSfxDef> g_catalog;   // built by install_adlib_sfx_voices()

}  // namespace

void install_adlib_sfx_voices(const prepare::AdlibSfxVoices& voices) {
    auto to_voice = [](const prepare::AdlibSfxVoice& v) {
        OplSfxDef d{};
        for (int i = 0; i < 13; ++i) {
            d.modulator.b[i] = v.mod[static_cast<std::size_t>(i)];
            d.carrier.b[i] = v.car[static_cast<std::size_t>(i)];
        }
        d.modulator_waveform = v.mod_wf;
        d.carrier_waveform = v.car_wf;
        return d;
    };
    g_catalog.clear();
    for (const SfxParams& p : kParams) {
        OplSfxDef d = to_voice(std::string(p.id) == "SFX_HIT" ? voices.hit
                               : std::string(p.id) == "SFX_JUMP_APEX"
                                   ? voices.jump_apex
                                   : voices.generic);
        d.id = p.id;
        d.note = p.note;
        d.velocity = p.velocity;
        d.channel = p.channel;
        d.fb_alg = p.fb_alg;
        d.gate_ms = p.gate_ms;
        d.tail_ms = p.tail_ms;
        g_catalog.push_back(d);
    }
}

std::vector<std::int16_t> render_adlib_sfx(const OplSfxVoice& modulator,
                                           const OplSfxVoice& carrier,
                                           int modulator_waveform,
                                           int carrier_waveform,
                                           int note, int velocity, int channel,
                                           int fb_alg, int sample_rate,
                                           int gate_ms, int tail_ms) {
    if (channel < 0 || channel > 8) return {};
    if (modulator_waveform < 0 || modulator_waveform > 3) return {};
    if (carrier_waveform < 0 || carrier_waveform > 3) return {};
    if (sample_rate <= 0) return {};
    const int adj_note = std::max(0, note - 12);       // FUN_1fe0_02ea:02f5
    if (adj_note > 0x5f) return {};
    const int chan_vol = std::min(velocity, 0x7f);     // FUN_1fe0_0234 clip

    opl3_chip chip;
    std::memset(&chip, 0, sizeof(chip));
    OPL3_Reset(&chip, static_cast<uint32_t>(sample_rate));

    // 1. Reset registers (AdLib_Reset): all 0, then 0x08 and 0xBD explicit.
    for (int reg = 0; reg < 256; ++reg)
        OPL3_WriteReg(&chip, static_cast<uint16_t>(reg), 0);
    OPL3_WriteReg(&chip, 0x08, 0);
    OPL3_WriteReg(&chip, 0xBD, 0);

    // 2. Install the modulator + carrier voice patches (DS:0x1b69 lookup).
    const int mod_op = kOpOffsetByVoiceIdx[kVoiceIdxByChannel[channel][0]];
    const int car_op = kOpOffsetByVoiceIdx[kVoiceIdxByChannel[channel][1]];
    const OpRegs m = operator_registers(modulator, modulator_waveform, chan_vol);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0x20 + mod_op), m.r20);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0x40 + mod_op), m.r40);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0x60 + mod_op), m.r60);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0x80 + mod_op), m.r80);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0xE0 + mod_op), m.re0);
    const OpRegs c = operator_registers(carrier, carrier_waveform, chan_vol);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0x20 + car_op), c.r20);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0x40 + car_op), c.r40);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0x60 + car_op), c.r60);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0x80 + car_op), c.r80);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0xE0 + car_op), c.re0);

    // Channel feedback/algorithm byte (RC0+ch); DS:0x1bb5 per-voice.
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0xC0 + channel),
                  static_cast<uint8_t>(fb_alg & 0xff));

    // 3. Note-on: A0+ch fnum-low, B0+ch fnum-hi|block|key-on(0x20).
    int fnum = 0, block = 0;
    note_to_fnum_block(adj_note, fnum, block);
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0xA0 + channel),
                  static_cast<uint8_t>(fnum & 0xff));
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0xB0 + channel),
                  static_cast<uint8_t>(((fnum >> 8) & 0x03) |
                                       ((block & 0x07) << 2) | 0x20));

    auto render_into = [&](std::vector<std::int16_t>& out, int ms) {
        const long frames = static_cast<long>(
            static_cast<double>(sample_rate) * ms / 1000.0);
        if (frames <= 0) return;
        const std::size_t base = out.size();
        out.resize(base + static_cast<std::size_t>(frames) * 2);
        OPL3_GenerateStream(&chip, out.data() + base,
                            static_cast<uint32_t>(frames));
    };

    std::vector<std::int16_t> pcm;
    render_into(pcm, gate_ms);                          // 4. gate

    // 5. Key-off: clear B0+ch bit 5 → release phase.
    OPL3_WriteReg(&chip, static_cast<uint16_t>(0xB0 + channel),
                  static_cast<uint8_t>(((fnum >> 8) & 0x03) |
                                       ((block & 0x07) << 2)));
    render_into(pcm, tail_ms);                          // 6. tail
    return pcm;
}

const OplSfxDef* opl_sfx_lookup(const std::string& id) {
    for (const auto& d : g_catalog)
        if (id == d.id) return &d;
    return nullptr;
}

std::vector<std::string> opl_sfx_ids() {
    std::vector<std::string> ids;
    ids.reserve(g_catalog.size());
    for (const auto& d : g_catalog) ids.emplace_back(d.id);
    return ids;
}

std::vector<std::int16_t> render_adlib_sfx_by_id(const std::string& id,
                                                 int sample_rate) {
    const OplSfxDef* d = opl_sfx_lookup(id);
    if (d == nullptr) return {};
    return render_adlib_sfx(d->modulator, d->carrier, d->modulator_waveform,
                            d->carrier_waveform, d->note, d->velocity,
                            d->channel, d->fb_alg, sample_rate, d->gate_ms,
                            d->tail_ms);
}

}  // namespace olduvai::presentation
