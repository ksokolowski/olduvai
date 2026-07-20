// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OPL music path: FF 7F timbre decode (formats) + the EXE-faithful driver's
// register behaviour (presentation/opl_music) on synthetic MDI bytes.  PCM
// parity against the Python reference renderer is verified out-of-band via
// tools/opl_music_dump (game music PCM never enters the repo).
#include "doctest/doctest.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "formats/mdi.hpp"
#include "presentation/opl_music.hpp"

using olduvai::formats::MdiEventStream;
using olduvai::formats::MdiStreamEvent;
using olduvai::formats::parse_mdi_events;
using olduvai::presentation::OplMusicPlayer;

namespace {

void push_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

// A type-0x0001 timbre payload: mfr id, type, channel, 2×13-byte operator
// blocks, 2 waveform bytes (34 bytes total).
std::vector<std::uint8_t> timbre_payload(std::uint8_t channel) {
    std::vector<std::uint8_t> p = {0x00, 0x00, 0x3B, 0x00, 0x01, channel};
    // primary: ksl=1 mult=2 fb=3 atk=10 sus_lvl=4 sus_on=1 dec=5 rel=6
    //          tl=16 am=1 vib=0 ksr=1 conn=0(additive)
    const std::uint8_t prim[13] = {1, 2, 3, 10, 4, 1, 5, 6, 16, 1, 0, 1, 0};
    // secondary: ksl=0 mult=1 fb=0 atk=12 sus_lvl=2 sus_on=0 dec=7 rel=8
    //            tl=8 am=0 vib=1 ksr=0 conn=2(FM)
    const std::uint8_t sec[13] = {0, 1, 0, 12, 2, 0, 7, 8, 8, 0, 1, 0, 2};
    p.insert(p.end(), prim, prim + 13);
    p.insert(p.end(), sec, sec + 13);
    p.push_back(1);   // primary waveform
    p.push_back(2);   // secondary waveform
    return p;
}

// Synthetic single-track MDI: timbre on channel 0 → tempo → note on ch 0 →
// (96 ticks) note off → end of track.  Division 96.
std::vector<std::uint8_t> synthetic_mdi() {
    std::vector<std::uint8_t> m = {'M', 'T', 'h', 'd'};
    push_u32(m, 6);
    m.insert(m.end(), {0, 0, 0, 1, 0, 96});   // format 0, 1 track, div 96

    std::vector<std::uint8_t> t;
    const auto timbre = timbre_payload(0);
    t.push_back(0);           // delta
    t.insert(t.end(), {0xFF, 0x7F});
    t.push_back(static_cast<std::uint8_t>(timbre.size()));
    t.insert(t.end(), timbre.begin(), timbre.end());
    t.insert(t.end(), {0, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});  // 500000 us
    t.insert(t.end(), {0, 0x90, 60, 127});    // note on C4, full velocity
    t.insert(t.end(), {96, 0x80, 0, 0});      // implicit note off (quirk)
    t.insert(t.end(), {0, 0xFF, 0x2F, 0x00});

    m.insert(m.end(), {'M', 'T', 'r', 'k'});
    push_u32(m, static_cast<std::uint32_t>(t.size()));
    m.insert(m.end(), t.begin(), t.end());
    return m;
}

}  // namespace

TEST_CASE("parse_mdi_events decodes the FF 7F timbre block") {
    const MdiEventStream s = parse_mdi_events(synthetic_mdi());
    REQUIRE(s.valid);
    CHECK(s.division == 96);
    REQUIRE(s.events.size() == 4);   // timbre, tempo, note on, note off

    const auto& seq = s.events[0];
    CHECK(seq.kind == MdiStreamEvent::Kind::Seq);
    REQUIRE(seq.seq.has_timbre);
    CHECK(seq.seq.message_type == 0x0001);
    CHECK(seq.seq.channel == 0);
    CHECK(seq.seq.primary.key_scale_level == 1);
    CHECK(seq.seq.primary.frequency_multiplier == 2);
    CHECK(seq.seq.primary.feedback == 3);
    CHECK(seq.seq.primary.attack_rate == 10);
    CHECK(seq.seq.primary.sustain_level == 4);
    CHECK(seq.seq.primary.sustain_enabled);
    CHECK(seq.seq.primary.decay_rate == 5);
    CHECK(seq.seq.primary.release_rate == 6);
    CHECK(seq.seq.primary.total_level == 16);
    CHECK(seq.seq.primary.amplitude_modulation);
    CHECK(!seq.seq.primary.vibrato);
    CHECK(seq.seq.primary.key_scale_rate);
    CHECK(seq.seq.primary.connection_additive);   // byte 12 == 0
    CHECK(seq.seq.primary.waveform == 1);
    CHECK(seq.seq.secondary.total_level == 8);
    CHECK(!seq.seq.secondary.connection_additive);
    CHECK(seq.seq.secondary.waveform == 2);

    CHECK(s.events[1].kind == MdiStreamEvent::Kind::Tempo);
    CHECK(s.events[1].tempo_us == 500000);
    CHECK(s.events[2].status == 0x90);
    CHECK(s.events[3].tick == 96);
}

TEST_CASE("parse_mdi_events rejects malformed containers") {
    CHECK(!parse_mdi_events({}).valid);
    CHECK(!parse_mdi_events({'M', 'T', 'h', 'x', 0, 0, 0, 6, 0, 0, 0, 1, 0,
                             96})
               .valid);
}

TEST_CASE("driver writes the authored patch and keys the note") {
    OplMusicPlayer player(44100);
    std::vector<std::pair<int, int>> writes;
    bool capture = false;
    player.reg_tap = [&](int reg, int val) {
        if (capture) writes.emplace_back(reg, val);
    };
    capture = false;
    REQUIRE(player.open(synthetic_mdi()));
    capture = true;

    // Render enough to consume every event (~0.5 s at div 96).
    std::vector<std::int16_t> pcm(44100 * 2);
    player.render(44100, pcm.data());

    auto find = [&](int reg) -> int {
        for (const auto& [r, v] : writes) {
            if (r == reg) return v;
        }
        return -1;
    };

    // Timbre → operator registers.  Channel 0 = slots 0 (op 0) and 3 (op 3).
    // primary R20 = AM|sus|KSR|mult = 0x80|0x20|0x10|2 = 0xB2
    CHECK(find(0x20) == 0xB2);
    // primary R40 = ksl<<6 | tl = 0x40 | 16 = 0x50
    CHECK(find(0x40) == 0x50);
    // primary R60 = atk<<4 | dec = 0xA5;  R80 = sus<<4 | rel = 0x46
    CHECK(find(0x60) == 0xA5);
    CHECK(find(0x80) == 0x46);
    CHECK(find(0xE0) == 1);
    // secondary (op 3): R20 = vib|mult = 0x40|1 = 0x41; R40 = 8
    CHECK(find(0x23) == 0x41);
    CHECK(find(0x43) == 8);
    // channel C0 = feedback<<1 | additive = (3<<1)|1 = 7
    CHECK(find(0xC0) == 7);

    // Note on 60 at velocity 127: scaled TL == patch TL (identity at 0x7F),
    // pitch = fnum 690 block 3 → A0 = 178, B0 = key-on|block|fnum-hi = 0x2E.
    CHECK(find(0xA0) == 178);
    CHECK(find(0xB0) == 0x2E);

    // Note off clears the key-on bit: a later B0 write of block bits only.
    bool key_off_seen = false;
    for (const auto& [r, v] : writes) {
        if (r == 0xB0 && (v & 0x20) == 0) key_off_seen = true;
    }
    CHECK(key_off_seen);
}

TEST_CASE("one-shot render goes inactive after the release tail") {
    OplMusicPlayer player(44100);
    REQUIRE(player.open(synthetic_mdi()));
    player.set_loop(false);
    std::vector<std::int16_t> pcm(44100 * 2 * 3);
    player.render(44100 * 3, pcm.data());   // 3 s ≫ track + 350 ms tail
    CHECK(!player.active());
}

TEST_CASE("looped render stays active past the track end") {
    OplMusicPlayer player(44100);
    REQUIRE(player.open(synthetic_mdi()));
    player.set_loop(true);
    std::vector<std::int16_t> pcm(44100 * 2 * 3);
    player.render(44100 * 3, pcm.data());
    CHECK(player.active());
}
