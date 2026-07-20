// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Input replay + frame tracing — the native half of the cross-engine
// regression harness.  Both files use the reference engine's JSONL
// schemas verbatim, so any scripted playthrough can be replayed through
// both engines and diffed frame-by-frame:
//   inputs: {"time_ms":N,"key":"left|right|up|down|attack",
//            "action":"press|release"}   (frame = time_ms / 55)
//   trace:  one FrameState object per line (the reference field set).

#pragma once

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "systems/frame_runner.hpp"

namespace olduvai::systems { struct BossPlayerState; }

namespace olduvai::presentation {

class InputReplay {
public:
    bool load(const std::string& path);
    bool active() const { return !frames_.empty(); }
    int last_frame() const { return last_frame_; }
    // Key state for an absolute frame number.
    systems::FrameInputs at(int frame) const;

private:
    std::map<int, std::vector<std::pair<std::string, bool>>> frames_;
    int last_frame_ = 0;
};

class TraceWriter {
public:
    bool open(const std::string& path);
    ~TraceWriter();
    bool active() const { return f_ != nullptr; }
    void write(int frame, const systems::SystemsState& state);
    // Boss-arena trace line: same FrameState schema; fields with no boss
    // equivalent are 0, `energy` carries the boss health.  Mirrors the
    // reference implementation's boss-arena trace end-frame mapping.
    void write_boss(int frame, const systems::BossPlayerState& p,
                    int boss_health);

private:
    std::FILE* f_ = nullptr;
};

// Records the live per-frame inputs back into the same JSONL schema
// InputReplay::load reads, so a played (or replayed) session can be turned
// into a reusable scenario.  Mirrors `op play --record-inputs`.
//
// For every key whose state changed since the previous frame it emits one
//   {"time_ms":T,"key":K,"action":"press"|"release"}
// line.  `T` is the INVERSE of the reader's frame→time_ms map
// (time_ms = frame * 55), so the reader folds the event back onto the same
// frame.  The caller hands in the frame index the reader will resolve this
// input at — i.e. `frame + 1` when the game loop reads `replay.at(frame+1)`
// — so a record-while-replaying round-trips byte-for-byte.
class InputRecorder {
public:
    bool open(const std::string& path);
    ~InputRecorder();
    bool active() const { return f_ != nullptr; }
    // `reader_frame` is the absolute frame the reader must resolve this input
    // at (see class comment).  Emits one line per changed key.
    void record(int reader_frame, const systems::FrameInputs& in);

private:
    std::FILE* f_ = nullptr;
    bool first_ = true;
    bool prev_[5] = {false, false, false, false, false};
};

}  // namespace olduvai::presentation
