// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/midi_seq.hpp"

#include <algorithm>   // std::min

namespace olduvai::presentation {

namespace {
std::size_t read_vlq(const std::vector<std::uint8_t>& d, std::size_t& pos,
                     std::size_t end) {
    std::size_t v = 0;
    while (pos < end) {   // end <= d.size(); stops inside the track bounds
        const std::uint8_t b = d[pos++];
        v = (v << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) break;
    }
    return v;
}
}  // namespace

bool MidiSequencer::load(const std::vector<std::uint8_t>& midi) {
    events_.clear();
    reset();
    if (midi.size() < 14 || midi[0] != 'M' || midi[1] != 'T') return false;
    division_ = (midi[12] << 8) | midi[13];
    if (division_ <= 0) division_ = 96;
    const std::size_t header_len = (static_cast<std::size_t>(midi[4]) << 24) |
                                   (midi[5] << 16) | (midi[6] << 8) | midi[7];
    std::size_t pos = 8 + header_len;
    if (pos + 8 > midi.size() || midi[pos] != 'M') return false;
    const std::size_t track_len =
        (static_cast<std::size_t>(midi[pos + 4]) << 24) |
        (midi[pos + 5] << 16) | (midi[pos + 6] << 8) | midi[pos + 7];
    pos += 8;
    const std::size_t end = std::min(pos + track_len, midi.size());

    std::uint32_t tick = 0;
    int running = -1;
    // Event-count ceiling: a hostile / amplified track can decode into far more
    // events than its byte length would suggest (running-status streams,
    // converter expansion) — real Prehistorik tracks hold a few thousand, so a
    // 1M cap is ~100x headroom yet bounds memory (~16 MB) instead of OOM-ing on
    // crafted input (fuzz_formats finding: events_ grew to 384 MB).
    constexpr std::size_t kMaxEvents = 1u << 20;
    // Every read below is bounded against `end` — a truncated / hostile track
    // can leave a status byte with its data bytes (or a meta length) running
    // off the buffer; each such read must break, not over-read past the end
    // (fuzz_formats finding: heap over-read at the d1/d2 and meta-type reads).
    // read_vlq stops at `end` too (its own bounds guard).
    // Iteration ceiling: a hostile track can reach a state where `pos` fails to
    // advance — or oscillates (a `continue` path consuming zero net bytes) —
    // spinning the bounded `while (pos < end)` loop forever (fuzz_formats: two
    // distinct crafted .MDIs hung seq.load, one a stall, one a cycle an
    // equality-only progress check missed). Valid parsing consumes >=1 byte per
    // iteration, so it never runs more than `end` times; cap there. Inert for
    // real tracks; bounds time absolutely regardless of the exact path.
    const std::size_t max_iters = end + 16;
    std::size_t iters = 0;
    while (pos < end) {
        if (++iters > max_iters) break;
        if (events_.size() >= kMaxEvents) break;
        tick += static_cast<std::uint32_t>(read_vlq(midi, pos, end));
        if (pos >= end) break;
        int status = midi[pos];
        if (status < 0x80) {
            if (running < 0) break;
            status = running;
        } else {
            ++pos;
            if (status < 0xF0) running = status;
        }
        if (status == 0xFF) {
            if (pos >= end) break;
            const std::uint8_t type = midi[pos++];
            const std::size_t len = read_vlq(midi, pos, end);
            if (type == 0x51 && len == 3 && pos + 3 <= end) {
                MidiEventMsg t;
                t.tick = tick;
                t.tempo = (static_cast<std::uint32_t>(midi[pos]) << 16) |
                          (midi[pos + 1] << 8) | midi[pos + 2];
                events_.push_back(t);
            }
            pos = std::min(pos + len, end);
            continue;
        }
        if (status >= 0xF0) {
            const std::size_t len = read_vlq(midi, pos, end);
            pos = std::min(pos + len, end);
            continue;
        }
        const int et = status & 0xF0;
        if (pos >= end) break;
        MidiEventMsg m;
        m.tick = tick;
        m.status = static_cast<std::uint8_t>(status);
        m.d1 = midi[pos++];
        if (et != 0xC0 && et != 0xD0) {
            if (pos >= end) break;
            m.d2 = midi[pos++];
        }
        events_.push_back(m);
    }
    // Capture the loop-point state once: the first event's tick (so leading
    // silence isn't replayed as a per-loop gap) and the tempo in force at
    // tick 0 (the first SET_TEMPO at tick 0, else the 120-BPM default).  The
    // converter prepends program/CC presets at tick 0, so events_[0] is tick 0
    // for our tracks, but this stays correct for any input.
    if (!events_.empty()) {
        first_event_tick_ = events_.front().tick;
        for (const auto& e : events_) {
            if (e.tick != 0) break;        // past tick 0: nothing earlier wins
            if (e.tempo != 0) {            // first tempo at tick 0 = effective
                initial_tempo_ = e.tempo;
                break;
            }
        }
        tempo_ = initial_tempo_;
        last_tick_ = first_event_tick_;
    }
    return !events_.empty();
}

void MidiSequencer::reset() {
    idx_ = 0;
    last_tick_ = 0;
    tempo_ = 500000;
    initial_tempo_ = 500000;
    first_event_tick_ = 0;
    frac_ = 0;
}

}  // namespace olduvai::presentation
