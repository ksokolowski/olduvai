// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Minimal MIDI sequencer: parses a format-0 stream into timed channel messages
// and serves them sample-accurately at an output rate.  Tempo defaults to
// 500000 us/qn and follows FF 51.  The loop is seamless, like the EXE
// dispatcher (stream pointer back to DS:0x8834 with no silence):
//   1. the track's initial tempo is restored at the seam, not the 120 BPM
//      default (BONUS.MDI is 155 BPM, SET_TEMPO at tick 0);
//   2. all-notes-off (CC 123) on all 16 channels at the seam, so hung notes do
//      not collide with the restart;
//   3. leading silence (first event tick > 0) is skipped at the seam.

#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::presentation {

struct MidiEventMsg {
    std::uint32_t tick = 0;
    std::uint8_t status = 0, d1 = 0, d2 = 0;
    std::uint32_t tempo = 0;   // non-zero = tempo meta (µs per quarter)
};

class MidiSequencer {
public:
    bool load(const std::vector<std::uint8_t>& midi);
    void reset();
    bool loaded() const { return !events_.empty(); }

    // Test/inspection accessors.
    std::uint32_t tempo() const { return tempo_; }
    std::uint32_t initial_tempo() const { return initial_tempo_; }
    std::uint32_t first_event_tick() const { return first_event_tick_; }

    // Advance `frames` samples at `rate`; invoke fn(status,d1,d2) for
    // every message that falls due.
    template <typename Fn>
    void advance(int frames, int rate, Fn&& fn) {
        if (events_.empty()) return;
        double samples_per_tick =
            static_cast<double>(tempo_) / 1e6 * rate / division_;
        double budget = frames;
        while (budget > 0) {
            if (idx_ >= events_.size()) {   // seamless loop
                // All notes off before the restart (instant, no gap).
                for (int chn = 0; chn < 16; ++chn) {
                    fn(static_cast<std::uint8_t>(0xB0 | chn), 123, 0);
                }
                idx_ = 0;
                // Restart at the first event's tick, not 0, so any leading
                // silence isn't replayed as a per-loop gap.
                last_tick_ = first_event_tick_;
                frac_ = 0;
                // The track's initial tempo, not the 120 BPM default.
                tempo_ = initial_tempo_;
                samples_per_tick =
                    static_cast<double>(tempo_) / 1e6 * rate / division_;
            }
            const auto& e = events_[idx_];
            const double wait =
                (e.tick - last_tick_) * samples_per_tick - frac_;
            if (wait > budget) {
                frac_ += budget;
                break;
            }
            budget -= wait > 0 ? wait : 0;
            frac_ = 0;
            last_tick_ = e.tick;
            if (e.tempo != 0) {
                tempo_ = e.tempo;
                samples_per_tick =
                    static_cast<double>(tempo_) / 1e6 * rate / division_;
            } else {
                fn(e.status, e.d1, e.d2);
            }
            ++idx_;
        }
    }

private:
    void capture_loop_point_();

    std::vector<MidiEventMsg> events_;
    int division_ = 96;
    std::size_t idx_ = 0;
    std::uint32_t last_tick_ = 0;
    std::uint32_t tempo_ = 500000;
    std::uint32_t initial_tempo_ = 500000;   // tempo in force at the loop point
    std::uint32_t first_event_tick_ = 0;     // tick of the first event
    double frac_ = 0;
};

}  // namespace olduvai::presentation
