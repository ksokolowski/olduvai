// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Input replay + frame tracing for the cross-engine harness, in the reference
// engine's JSONL schemas:
//   inputs: {"time_ms":N,"key":"left|right|up|down|attack",
//            "action":"press|release"}   (frame = time_ms / 55)
//   trace:  one FrameState object per line.

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
    TraceWriter() = default;
    ~TraceWriter();
    TraceWriter(const TraceWriter&) = delete;
    TraceWriter& operator=(const TraceWriter&) = delete;
    bool active() const { return f_ != nullptr; }
    void write(int frame, const systems::SystemsState& state);
    // Boss-arena trace line: the same schema; fields without a boss equivalent
    // are 0, `energy` holds the boss health.
    void write_boss(int frame, const systems::BossPlayerState& p,
                    int boss_health);

private:
    std::FILE* f_ = nullptr;
};

// Record the live inputs in InputReplay's schema (--record-inputs): one
// {"time_ms":T,"key":K,"action":"press"|"release"} line per key change, with
// T = frame * 55 (the reader's inverse).  Pass the frame the reader will
// resolve (frame + 1 when the loop reads replay.at(frame+1)), so recording a
// replay round-trips byte for byte.
class InputRecorder {
public:
    bool open(const std::string& path);
    InputRecorder() = default;
    ~InputRecorder();
    InputRecorder(const InputRecorder&) = delete;
    InputRecorder& operator=(const InputRecorder&) = delete;
    bool active() const { return f_ != nullptr; }
    // `reader_frame` is the absolute frame the reader must resolve this input
    // at (see class comment).  Emits one line per changed key.
    void record(int reader_frame, const systems::FrameInputs& in);

private:
    std::FILE* f_ = nullptr;
    bool first_ = true;
    bool prev_[5] = {false, false, false, false, false};
};

// The per-run harness files: the --replay script, --trace output,
// --record-inputs output.  Empty paths leave a part inactive.
struct RunCapture {
    InputReplay replay;
    TraceWriter trace;
    InputRecorder input_rec;

    void open(const std::string& replay_path, const std::string& trace_path,
              const std::string& record_inputs_path);
};

}  // namespace olduvai::presentation
