// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "formats/mdi.hpp"

#include <algorithm>
#include <array>
#include <map>

namespace olduvai::formats {

namespace {

std::size_t read_vlq(const std::vector<std::uint8_t>& d, std::size_t& pos) {
    std::size_t value = 0;
    while (pos < d.size()) {
        const std::uint8_t b = d[pos++];
        value = (value << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) break;
    }
    return value;
}

void write_vlq(std::vector<std::uint8_t>& out, std::size_t value) {
    // SMF caps a variable-length quantity at 4 bytes (max 0x0FFFFFFF).  A
    // corrupt/hostile container can accumulate absurd tick deltas; clamp to
    // the spec max instead of overflowing the byte stack below (a mutated
    // .MDI overran stack[5] here — fuzz_formats finding).  Real containers
    // never get near the cap, so faithful output is unchanged.
    if (value > 0x0FFFFFFF) value = 0x0FFFFFFF;
    std::uint8_t stack[5];
    int n = 0;
    stack[n++] = value & 0x7F;
    value >>= 7;
    while (value != 0) {
        stack[n++] = 0x80 | (value & 0x7F);
        value >>= 7;
    }
    while (n > 0) out.push_back(stack[--n]);
}

std::uint32_t be32(const std::vector<std::uint8_t>& d, std::size_t pos) {
    return (static_cast<std::uint32_t>(d[pos]) << 24) |
           (static_cast<std::uint32_t>(d[pos + 1]) << 16) |
           (static_cast<std::uint32_t>(d[pos + 2]) << 8) | d[pos + 3];
}

}  // namespace

// Per-track MT-32 program assignment for the melodic-synth path.  The MDI
// streams carry no program-change events; this channel→program table is the
// release catalog's curated assignment, matched per track against the
// original driver's MT-32 output (it is not a table in the executable —
// byte-searched 2026-07-19).
const std::map<int, int>* roland_program_map(int track_id) {
    static const std::map<int, std::map<int, int>> kMap = {
        {0, {{1, 14}, {2, 34}, {3, 32}, {4, 56}, {5, 77}, {10, 0}}},
        {1, {{1, 69}, {2, 26}, {3, 44}, {4, 30}, {5, 24}, {6, 63}, {10, 0}}},
        {2, {{1, 28}, {2, 98}, {3, 44}, {4, 30}, {5, 117}, {6, 37}, {10, 0}}},
        {3, {{1, 66}, {2, 92}, {3, 39}, {4, 30}, {5, 104}, {6, 43}, {10, 0}}},
        {4, {{1, 30}, {2, 78}, {3, 15}, {4, 95}, {5, 122}, {10, 0}}},
        {5, {{1, 30}, {2, 78}, {3, 15}, {4, 39}, {5, 87}, {10, 0}}},
        {6, {{1, 63}, {2, 98}, {3, 44}, {4, 7}, {5, 43}, {6, 113}, {10, 0}}},
        {7, {{1, 8}, {2, 98}, {3, 44}, {4, 30}, {5, 43}, {6, 25}, {10, 0}}},
        {8, {{1, 30}, {2, 97}, {3, 43}, {4, 68}, {5, 98}, {6, 51}, {10, 0}}},
    };
    const auto it = kMap.find(track_id);
    return it == kMap.end() ? nullptr : &it->second;
}

// EXE 'R'-branch MT-32 channel remap (FUN_1f75_01bb jump-table arms): MDI
// channel → output channel.  Melody parts 0..5 → 1..6; channels 6..15 collapse
// onto channel 9 (the rhythm/percussion part — drums in both MT-32 and GM).
// Tracks 0/4/5 collapse some melody channels too (composer's "monaural"
// tricks).  Mirrors the reference implementation's MT-32 channel remap tables.  Applied only on
// the melodic-synth path (mt32_strict); the OPL/'A' branch keeps raw channels.
const std::array<int, 16>& channel_remap(int track_id) {
    static const std::array<int, 16> kDefault =
        {1, 2, 3, 4, 5, 6, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    static const std::array<int, 16> k0 =   // mort.mdi — slot 5 → part 5
        {1, 2, 3, 4, 5, 5, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    static const std::array<int, 16> k4 =   // rocky.mdi — slots 4&5 → part 4
        {1, 2, 3, 4, 4, 4, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    static const std::array<int, 16> k5 =   // boy16.mdi — slot 5 → part 2
        {1, 2, 3, 4, 5, 2, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    switch (track_id) {
        case 0:  return k0;
        case 4:  return k4;
        case 5:  return k5;
        default: return kDefault;
    }
}

// Approximate MT-32 -> GM preset translation (timbres without an exact
// GM peer map to the closest family member; authentic sound needs the
// real MT-32 path).
int mt32_to_gm(int program) {
    static const std::map<int, int> kMap = {
        {7, 3}, {8, 16}, {14, 19}, {15, 21}, {24, 62}, {25, 62},  // 25/26: ScummVM orientation (owner indifferent SynBrass1↔2)
        {26, 63}, {28, 38}, {30, 39}, {32, 88}, {34, 52}, {37, 99},
        {39, 95}, {43, 90}, {44, 81}, {51, 45}, {56, 43}, {63, 104},  // 39: owner ear-check vs MT-32 (was 86); see mt32_to_gm_scummvm_walk.md
        {66, 33}, {68, 36}, {69, 37}, {77, 75}, {78, 65}, {87, 22},
        {92, 60}, {95, 61}, {97, 11}, {98, 11}, {104, 12}, {113, 117},
        {117, 116}, {122, 55},
    };
    const auto it = kMap.find(program);
    return it == kMap.end() ? program : it->second;
}

int mdi_track_id(const std::string& n) {
    static const std::map<std::string, int> kIds = {
        {"mort.mdi", 0}, {"bonus.mdi", 1}, {"bonusbuz.mdi", 1},
        {"rik1.mdi", 2}, {"rik6.mdi", 3}, {"rocky.mdi", 4},
        {"boy16.mdi", 5}, {"intro.mdi", 6}, {"fin.mdi", 7},
        {"rik8.mdi", 8},
    };
    const auto it = kIds.find(n);
    return it == kIds.end() ? -1 : it->second;
}

std::vector<std::uint8_t> build_gm_midi(const std::vector<std::uint8_t>& raw,
                                        int track_id, bool mt32_strict,
                                        bool gm_translate) {
    if (raw.size() < 14 || raw[0] != 'M' || raw[1] != 'T' || raw[2] != 'h' ||
        raw[3] != 'd') {
        return raw;
    }
    const std::size_t header_len = be32(raw, 4);
    std::size_t pos = 8 + header_len;
    if (pos + 8 > raw.size() || raw[pos] != 'M' || raw[pos + 1] != 'T' ||
        raw[pos + 2] != 'r' || raw[pos + 3] != 'k') {
        return raw;
    }
    const std::size_t track_len = be32(raw, pos + 4);
    const std::size_t track_start = pos + 8;
    std::vector<std::uint8_t> track(
        raw.begin() + static_cast<std::ptrdiff_t>(track_start),
        raw.begin() + static_cast<std::ptrdiff_t>(
            std::min(track_start + track_len, raw.size())));

    std::vector<std::uint8_t> ev;
    // MT-32 'R'-branch channel remap (melody 0-5 → 1-6, percussion 6-15 → 9 =
    // the rhythm/drum channel).  Applied for BOTH melodic synths (mt32_strict =
    // MT-32 or GM) — the EXE 'R'/MPU-401 branch installs it (FUN_1f75_01bb);
    // owner A/B confirmed the remapped MT-32 is the correct sound.  OPL keeps
    // raw channels (the 'A' branch).  Note-tracking below stays on the ORIGINAL
    // channel so the percussion channels collapsing onto 9 don't auto-cancel.
    const std::array<int, 16>& remap = channel_remap(track_id);
    auto rch = [&](int ch) -> int {
        return mt32_strict ? remap[static_cast<std::size_t>(ch & 0x0F)] : ch;
    };
    // Inject the per-track program presets + full channel volume.
    if (const auto* programs = roland_program_map(track_id)) {
        for (const auto& [ch, prog] : *programs) {
            const int oc = rch(ch - 1);
            write_vlq(ev, 0);
            ev.push_back(static_cast<std::uint8_t>(0xC0 | oc));
            ev.push_back(static_cast<std::uint8_t>(
                gm_translate ? mt32_to_gm(prog) : prog));
            write_vlq(ev, 0);
            ev.push_back(static_cast<std::uint8_t>(0xB0 | oc));
            ev.push_back(7);
            ev.push_back(127);
        }
    }

    int last_note[16];
    for (int& v : last_note) v = 0x80;

    std::size_t tpos = 0;
    int running_status = -1;
    std::size_t accumulated_delta = 0;
    auto emit = [&](const std::uint8_t* payload, std::size_t n) {
        write_vlq(ev, accumulated_delta);
        ev.insert(ev.end(), payload, payload + n);
        accumulated_delta = 0;
    };

    while (tpos < track.size()) {
        accumulated_delta += read_vlq(track, tpos);
        if (tpos >= track.size()) break;
        int status;
        const std::uint8_t first = track[tpos];
        if (first < 0x80) {
            if (running_status < 0) break;
            status = running_status;
        } else {
            status = first;
            ++tpos;
            if (status < 0xF0) running_status = status;
        }
        if (status == 0xFF) {
            if (tpos >= track.size()) break;
            const std::uint8_t meta_type = track[tpos++];
            // Clamp the declared length to the bytes present: an unbounded
            // VLQ from a corrupt file can wrap `tpos` below `body` (reversed
            // insert range = UB) or rewind the cursor into an endless loop.
            const std::size_t declared_len = read_vlq(track, tpos);
            const std::size_t meta_len =
                std::min(declared_len, track.size() - tpos);
            const std::size_t body = tpos;
            tpos += meta_len;
            if (meta_type == 0x7F) continue;   // strip OPL timbre blocks
            std::vector<std::uint8_t> payload = {0xFF, meta_type};
            write_vlq(payload, meta_len);
            payload.insert(payload.end(),
                           track.begin() + static_cast<std::ptrdiff_t>(body),
                           track.begin() + static_cast<std::ptrdiff_t>(tpos));
            emit(payload.data(), payload.size());
            continue;
        }
        if (status >= 0xF0) {   // sysex — skipped entirely
            const std::size_t declared_sl = read_vlq(track, tpos);
            tpos += std::min(declared_sl, track.size() - tpos);
            continue;
        }
        const int event_type = status & 0xF0;
        const int ch = status & 0x0F;          // original — for note tracking
        const int oc = rch(ch);                // remapped — for output
        int d1 = 0, d2 = 0;
        if (event_type == 0x80 || event_type == 0x90 || event_type == 0xA0 ||
            event_type == 0xB0 || event_type == 0xE0) {
            if (tpos + 1 >= track.size()) break;
            d1 = track[tpos];
            d2 = track[tpos + 1];
            tpos += 2;
        } else if (event_type == 0xC0 || event_type == 0xD0) {
            if (tpos >= track.size()) break;
            d1 = track[tpos];
            ++tpos;
        } else {
            continue;
        }
        if (mt32_strict && (event_type == 0xA0 || event_type == 0xB0 ||
                            event_type == 0xD0 || event_type == 0xE0)) {
            continue;
        }
        if (event_type == 0x90 && d2 != 0) {
            // Auto-release a held note, then emit + remember.
            if (last_note[ch] < 0x80) {
                const std::uint8_t off[3] = {
                    static_cast<std::uint8_t>(0x80 | oc),
                    static_cast<std::uint8_t>(last_note[ch]), 0x00};
                emit(off, 3);
            }
            const std::uint8_t on[3] = {static_cast<std::uint8_t>(0x90 | oc),
                                        static_cast<std::uint8_t>(d1),
                                        static_cast<std::uint8_t>(d2)};
            emit(on, 3);
            last_note[ch] = d1;
        } else if (event_type == 0x90) {   // vel 0 = release
            const int prev = last_note[ch];
            if (prev < 0x80) {
                const std::uint8_t off[3] = {
                    static_cast<std::uint8_t>(0x80 | oc),
                    static_cast<std::uint8_t>(prev), 0x00};
                emit(off, 3);
            }
            last_note[ch] = 0x80;
        } else if (event_type == 0x80) {
            const int note = d1 != 0 ? d1 : last_note[ch];
            if (note < 0x80) {
                const std::uint8_t off[3] = {
                    static_cast<std::uint8_t>(0x80 | oc),
                    static_cast<std::uint8_t>(note),
                    static_cast<std::uint8_t>(d2 & 0x7F)};
                emit(off, 3);
                last_note[ch] = 0x80;
            }
        } else {
            if (event_type == 0xC0 || event_type == 0xD0) {
                const std::uint8_t p[2] = {
                    static_cast<std::uint8_t>(event_type | oc),
                    static_cast<std::uint8_t>(d1)};
                emit(p, 2);
            } else {
                const std::uint8_t p[3] = {
                    static_cast<std::uint8_t>(event_type | oc),
                    static_cast<std::uint8_t>(d1),
                    static_cast<std::uint8_t>(d2)};
                emit(p, 3);
            }
        }
    }
    // End-of-track meta.
    const bool has_eot =
        ev.size() >= 3 && ev[ev.size() - 3] == 0xFF &&
        ev[ev.size() - 2] == 0x2F && ev[ev.size() - 1] == 0x00;
    if (!has_eot) {
        write_vlq(ev, 0);
        ev.push_back(0xFF);
        ev.push_back(0x2F);
        ev.push_back(0x00);
    }
    std::vector<std::uint8_t> out(
        raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(8 + header_len));
    out.push_back('M');
    out.push_back('T');
    out.push_back('r');
    out.push_back('k');
    const std::uint32_t n = static_cast<std::uint32_t>(ev.size());
    out.push_back(static_cast<std::uint8_t>(n >> 24));
    out.push_back(static_cast<std::uint8_t>(n >> 16));
    out.push_back(static_cast<std::uint8_t>(n >> 8));
    out.push_back(static_cast<std::uint8_t>(n));
    out.insert(out.end(), ev.begin(), ev.end());
    return out;
}

namespace {

// Decode a 13-byte operator block + waveform byte (mirrors the reference
// implementation's OPL state-block decode).
MdiOplOperatorState decode_opl_block(const std::uint8_t* f,
                                     std::uint8_t waveform_byte) {
    MdiOplOperatorState s;
    s.key_scale_level = f[0] & 0x03;
    s.frequency_multiplier = f[1] & 0x0F;
    s.feedback = f[2] & 0x07;
    s.attack_rate = f[3] & 0x0F;
    s.sustain_level = f[4] & 0x0F;
    s.sustain_enabled = f[5] != 0;
    s.decay_rate = f[6] & 0x0F;
    s.release_rate = f[7] & 0x0F;
    s.total_level = f[8] & 0x3F;
    s.amplitude_modulation = f[9] != 0;
    s.vibrato = f[10] != 0;
    s.key_scale_rate = f[11] != 0;
    s.connection_additive = f[12] == 0;
    s.waveform = waveform_byte & 0x03;
    return s;
}

// Decode an FF 7F payload (mirrors `_decode_sequencer_specific_event`).
MdiSeqEvent decode_seq_event(const std::uint8_t* p, std::size_t n) {
    MdiSeqEvent e;
    if (n < 5) return e;   // message_type stays -1 (undecoded)
    e.message_type = (static_cast<int>(p[3]) << 8) | p[4];
    if (n == 6 && (e.message_type == 0x0002 || e.message_type == 0x0003)) {
        e.value = p[5];
    } else if (n == 34 && e.message_type == 0x0001) {
        e.channel = p[5];
        const std::uint8_t* body = p + 6;
        e.primary = decode_opl_block(body, body[26]);
        e.secondary = decode_opl_block(body + 13, body[27]);
        e.has_timbre = true;
    }
    return e;
}

}  // namespace

MdiEventStream parse_mdi_events(const std::vector<std::uint8_t>& raw) {
    MdiEventStream out;
    if (raw.size() < 14 || raw[0] != 'M' || raw[1] != 'T' || raw[2] != 'h' ||
        raw[3] != 'd') {
        return out;
    }
    const std::size_t header_len = be32(raw, 4);
    if (header_len < 6 || 8 + header_len > raw.size()) return out;
    out.division = (static_cast<int>(raw[12]) << 8) | raw[13];
    const int n_tracks = (static_cast<int>(raw[10]) << 8) | raw[11];

    std::size_t pos = 8 + header_len;
    for (int t = 0; t < n_tracks; ++t) {
        if (pos + 8 > raw.size() || raw[pos] != 'M' || raw[pos + 1] != 'T' ||
            raw[pos + 2] != 'r' || raw[pos + 3] != 'k') {
            return MdiEventStream{};   // malformed track chunk
        }
        const std::size_t track_len = be32(raw, pos + 4);
        const std::size_t ts = pos + 8;
        if (ts + track_len > raw.size()) return MdiEventStream{};
        const std::size_t te = ts + track_len;
        pos = te;

        std::size_t i = ts;
        std::uint32_t tick = 0;
        int running_status = -1;
        while (i < te) {
            // Delta VLQ.
            std::uint32_t delta = 0;
            while (i < te) {
                const std::uint8_t b = raw[i++];
                delta = (delta << 7) | (b & 0x7F);
                if ((b & 0x80) == 0) break;
            }
            tick += delta;
            if (i >= te) break;

            int status = raw[i];
            if (status < 0x80) {
                if (running_status < 0) return MdiEventStream{};
                status = running_status;
            } else {
                ++i;
                running_status = status < 0xF0 ? status : -1;
            }

            if (status == 0xFF) {   // meta
                if (i >= te) return MdiEventStream{};
                const std::uint8_t meta_type = raw[i++];
                std::size_t mlen = 0;
                while (i < te) {
                    const std::uint8_t b = raw[i++];
                    mlen = (mlen << 7) | (b & 0x7F);
                    if ((b & 0x80) == 0) break;
                }
                mlen = std::min(mlen, te - i);
                if (meta_type == 0x51 && mlen == 3) {
                    MdiStreamEvent e;
                    e.tick = tick;
                    e.kind = MdiStreamEvent::Kind::Tempo;
                    e.tempo_us = (static_cast<std::uint32_t>(raw[i]) << 16) |
                                 (static_cast<std::uint32_t>(raw[i + 1]) << 8) |
                                 raw[i + 2];
                    out.events.push_back(e);
                } else if (meta_type == 0x7F) {
                    MdiStreamEvent e;
                    e.tick = tick;
                    e.kind = MdiStreamEvent::Kind::Seq;
                    e.seq = decode_seq_event(raw.data() + i, mlen);
                    out.events.push_back(e);
                }
                i += mlen;
                continue;
            }
            if (status == 0xF0 || status == 0xF7) {   // sysex — skip
                std::size_t slen = 0;
                while (i < te) {
                    const std::uint8_t b = raw[i++];
                    slen = (slen << 7) | (b & 0x7F);
                    if ((b & 0x80) == 0) break;
                }
                i += std::min(slen, te - i);
                continue;
            }

            const int event_type = status & 0xF0;
            MdiStreamEvent e;
            e.tick = tick;
            e.kind = MdiStreamEvent::Kind::Channel;
            e.status = static_cast<std::uint8_t>(status);
            if (event_type == 0x80 || event_type == 0x90 ||
                event_type == 0xA0 || event_type == 0xB0 ||
                event_type == 0xE0) {
                if (i + 1 >= te) return MdiEventStream{};
                e.data1 = raw[i];
                e.data2 = raw[i + 1];
                i += 2;
            } else if (event_type == 0xC0 || event_type == 0xD0) {
                if (i >= te) return MdiEventStream{};
                e.data1 = raw[i];
                ++i;
            } else {
                return MdiEventStream{};   // unsupported status
            }
            out.events.push_back(e);
        }
    }
    out.valid = true;
    return out;
}

}  // namespace olduvai::formats
