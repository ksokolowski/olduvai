// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/audio/midi_seq.hpp"

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

std::size_t be32(const std::vector<std::uint8_t>& d, std::size_t i) {
    return (static_cast<std::size_t>(d[i]) << 24) | (d[i + 1] << 16) |
           (d[i + 2] << 8) | d[i + 3];
}

// A meta event, the cursor past its 0xFF and bounded by `end`: a tempo change
// becomes an event, anything else is skipped.  False when the track ends
// before its type.
bool read_meta(const std::vector<std::uint8_t>& midi, std::size_t& pos,
               std::size_t end, std::uint32_t tick,
               std::vector<MidiEventMsg>& events) {
    if (pos >= end) return false;
    const std::uint8_t type = midi[pos++];
    const std::size_t len = read_vlq(midi, pos, end);
    if (type == 0x51 && len == 3 && pos + 3 <= end) {
        MidiEventMsg t;
        t.tick = tick;
        t.tempo = (static_cast<std::uint32_t>(midi[pos]) << 16) |
                  (midi[pos + 1] << 8) | midi[pos + 2];
        events.push_back(t);
    }
    pos = std::min(pos + len, end);
    return true;
}

// A channel event's data bytes (one for program change and channel pressure,
// else two) into `m`.  False when the track ends inside them.
bool read_channel_event(const std::vector<std::uint8_t>& midi,
                        std::size_t& pos, std::size_t end, int status,
                        MidiEventMsg& m) {
    const int et = status & 0xF0;
    if (pos >= end) return false;
    m.status = static_cast<std::uint8_t>(status);
    m.d1 = midi[pos++];
    if (et != 0xC0 && et != 0xD0) {
        if (pos >= end) return false;
        m.d2 = midi[pos++];
    }
    return true;
}

}  // namespace

bool MidiSequencer::load(const std::vector<std::uint8_t>& midi) {
    events_.clear();
    reset();
    if (midi.size() < 14 || midi[0] != 'M' || midi[1] != 'T') return false;
    division_ = (midi[12] << 8) | midi[13];
    if (division_ <= 0) division_ = 96;
    std::size_t pos = 8 + be32(midi, 4);
    if (pos + 8 > midi.size() || midi[pos] != 'M') return false;
    const std::size_t track_len = be32(midi, pos + 4);
    pos += 8;
    const std::size_t end = std::min(pos + track_len, midi.size());

    std::uint32_t tick = 0;
    int running = -1;
    // Event cap: a crafted track can decode into far more events than its size
    // suggests (fuzz: 384 MB).  Real tracks hold a few thousand; 1M bounds
    // memory at ~16 MB.
    constexpr std::size_t kMaxEvents = 1u << 20;
    // Every read is bounded by `end` (read_vlq too): a truncated track can
    // leave a status byte or meta length running off the buffer.  Iteration
    // cap: a crafted track can stall or cycle `pos` (two fuzz finds); valid
    // parsing consumes >= 1 byte per iteration, so `end` iterations always
    // suffice.
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
            if (!read_meta(midi, pos, end, tick, events_)) break;
            continue;
        }
        if (status >= 0xF0) {
            const std::size_t len = read_vlq(midi, pos, end);
            pos = std::min(pos + len, end);
            continue;
        }
        MidiEventMsg m;
        m.tick = tick;
        if (!read_channel_event(midi, pos, end, status, m)) break;
        events_.push_back(m);
    }
    capture_loop_point_();
    return !events_.empty();
}

// Loop-point state, captured once: the first event's tick (leading silence is
// not replayed per loop) and the tempo in force at tick 0 (the first SET_TEMPO
// there, else 120 BPM).
void MidiSequencer::capture_loop_point_() {
    if (events_.empty()) return;
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

void MidiSequencer::reset() {
    idx_ = 0;
    last_tick_ = 0;
    tempo_ = 500000;
    initial_tempo_ = 500000;
    first_event_tick_ = 0;
    frac_ = 0;
}

}  // namespace olduvai::presentation
