// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// MidiSequencer loop-seam fidelity: the live mt32/fluidsynth streamer must
// loop *seamlessly* (matches the EXE MDI dispatcher + the Python reference's
// gapless PCM loop).  Regression guard for the "choppy BONUS.MDI loop" bug:
// the old code hard-reset tempo to the 120-BPM MIDI default and never cleared
// hung notes at the seam.

#include <vector>

#include "doctest/doctest.h"
#include "presentation/midi_seq.hpp"

using olduvai::presentation::MidiSequencer;

namespace {

void put_be32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

// Wrap a raw MTrk event byte-stream into a format-0 MThd+MTrk file at a
// given division.
std::vector<std::uint8_t> make_midi(int division,
                                    const std::vector<std::uint8_t>& track) {
    std::vector<std::uint8_t> m = {'M', 'T', 'h', 'd'};
    put_be32(m, 6);
    m.push_back(0);
    m.push_back(0);  // format 0
    m.push_back(0);
    m.push_back(1);  // 1 track
    m.push_back(static_cast<std::uint8_t>(division >> 8));
    m.push_back(static_cast<std::uint8_t>(division & 0xFF));
    m.insert(m.end(), {'M', 'T', 'r', 'k'});
    put_be32(m, static_cast<std::uint32_t>(track.size()));
    m.insert(m.end(), track.begin(), track.end());
    return m;
}

struct Emitted {
    std::uint8_t status, d1, d2;
};

}  // namespace

TEST_CASE("loop preserves the track's initial tempo (not the 120-BPM default)") {
    // SET_TEMPO 387096 (155 BPM, like BONUS.MDI) at tick 0, a program change,
    // a note on/off, then end.  division 420.
    const std::vector<std::uint8_t> track = {
        0x00, 0xFF, 0x51, 0x03, 0x05, 0xE8, 0x18,  // tempo 0x05E818 = 387096
        0x00, 0xC0, 28,                            // program change ch0
        0x00, 0x90, 60, 100,                       // note on
        0x10, 0x80, 60, 0,                         // note off
        0x00, 0xFF, 0x2F, 0x00,                    // EOT (ignored by loader)
    };
    MidiSequencer seq;
    REQUIRE(seq.load(make_midi(420, track)));

    CHECK(seq.initial_tempo() == 387096u);
    CHECK(seq.first_event_tick() == 0u);
    CHECK(seq.tempo() == 387096u);   // in force from the very first play

    // Drain far past end-of-track so the loop fires at least once.
    std::vector<Emitted> ev;
    for (int i = 0; i < 4; ++i) {
        seq.advance(48000, 48000,
                    [&](std::uint8_t s, std::uint8_t a, std::uint8_t b) {
                        ev.push_back({s, a, b});
                    });
    }
    // After looping, tempo is the track's 155-BPM value, NOT 500000.
    CHECK(seq.tempo() == 387096u);
}

TEST_CASE("loop emits all-notes-off on all 16 channels at the seam") {
    const std::vector<std::uint8_t> track = {
        0x00, 0x90, 60, 100,          // note on (left hung — no note-off)
        0x10, 0xC0, 1,                // a later event so the track has length
    };
    MidiSequencer seq;
    REQUIRE(seq.load(make_midi(96, track)));

    std::vector<Emitted> ev;
    // One big chunk guarantees we cross the seam at least once.
    seq.advance(96000, 48000,
                [&](std::uint8_t s, std::uint8_t a, std::uint8_t b) {
                    ev.push_back({s, a, b});
                });

    // Count the all-notes-off (CC 123, val 0) emitted on every channel at a
    // single seam.
    int an_off[16] = {0};
    for (const auto& e : ev) {
        if ((e.status & 0xF0) == 0xB0 && e.d1 == 123 && e.d2 == 0) {
            ++an_off[e.status & 0x0F];
        }
    }
    for (int chn = 0; chn < 16; ++chn) {
        CHECK(an_off[chn] >= 1);   // at least one full sweep at the seam
    }
}

TEST_CASE("no leading-silence gap: seam restarts at the first event tick") {
    // First event at tick 240 (leading silence).  The seam must restart at
    // tick 240, not 0, so the silence isn't replayed every loop.
    const std::vector<std::uint8_t> track = {
        0x81, 0x70, 0x90, 60, 100,    // delta 240 (VLQ 0x81 0x70), note on
        0x10, 0x80, 60, 0,            // note off
    };
    MidiSequencer seq;
    REQUIRE(seq.load(make_midi(96, track)));
    CHECK(seq.first_event_tick() == 240u);

    // Time-stamp every emitted note-on by counting samples consumed.  With a
    // seam-restart at tick 240 (first_event_tick), the inter-loop gap between
    // note-ons reflects only the 16 ticks from note-off to the next loop's
    // note-on — never the 240 ticks of leading silence (which a tick-0 restart
    // would replay every loop).
    int note_ons = 0;
    long samples = 0;
    long last_on_sample = -1;
    long max_gap = 0;
    const int rate = 48000;
    for (int i = 0; i < 400; ++i) {
        seq.advance(2000, rate,
                    [&](std::uint8_t s, std::uint8_t, std::uint8_t b) {
                        if ((s & 0xF0) == 0x90 && b != 0) {
                            ++note_ons;
                            if (last_on_sample >= 0) {
                                max_gap =
                                    std::max(max_gap, samples - last_on_sample);
                            }
                            last_on_sample = samples;
                        }
                    });
        samples += 2000;
    }
    REQUIRE(note_ons >= 3);
    // samples-per-tick at the default 500000 µs/qn, div 96.
    const double spt = 500000.0 / 1e6 * rate / 96.0;
    const long gap_16 = static_cast<long>(16 * spt);    // expected per-loop gap
    const long silence_240 = static_cast<long>(240 * spt);  // replayed-silence
    // Gap is bounded near the 16-tick figure (a buffer of <2000 samples) and
    // nowhere near the 240-tick leading-silence run.
    CHECK(max_gap < gap_16 + 2 * 2000);
    CHECK(max_gap < silence_240 / 2);
}
