// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/replay.hpp"

#include <fstream>

#include "systems/boss.hpp"

namespace olduvai::presentation {

namespace {
// Minimal extraction from one flat JSON object line.
std::string json_str(const std::string& line, const std::string& key) {
    const auto k = line.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    auto c = line.find(':', k);
    if (c == std::string::npos) return "";
    ++c;
    while (c < line.size() && (line[c] == ' ' || line[c] == '"')) ++c;
    std::string out;
    while (c < line.size() && line[c] != '"' && line[c] != ',' &&
           line[c] != '}') {
        out += line[c++];
    }
    return out;
}
}  // namespace

bool InputReplay::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::string key = json_str(line, "key");
        const std::string action = json_str(line, "action");
        const std::string tms = json_str(line, "time_ms");
        if (key.empty() || action.empty() || tms.empty()) continue;
        const int frame = std::atoi(tms.c_str()) / 55;   // 18 Hz ticks
        frames_[frame].emplace_back(key, action == "press");
        last_frame_ = std::max(last_frame_, frame);
    }
    return !frames_.empty();
}

systems::FrameInputs InputReplay::at(int frame) const {
    // Fold all events up to `frame` into key state.
    systems::FrameInputs in;
    bool state[5] = {false, false, false, false, false};
    static const char* kKeys[5] = {"left", "right", "up", "down", "attack"};
    for (const auto& [f, events] : frames_) {
        if (f > frame) break;
        for (const auto& [key, pressed] : events) {
            for (int i = 0; i < 5; ++i) {
                if (key == kKeys[i]) state[i] = pressed;
            }
        }
    }
    in.left = state[0];
    in.right = state[1];
    in.up = state[2];
    in.down = state[3];
    in.attack = state[4];
    return in;
}

bool TraceWriter::open(const std::string& path) {
    f_ = std::fopen(path.c_str(), "w");
    return f_ != nullptr;
}

TraceWriter::~TraceWriter() {
    if (f_ != nullptr) std::fclose(f_);
}

void TraceWriter::write(int frame, const systems::SystemsState& s) {
    if (f_ == nullptr) return;
    // The reference FrameState field set, in its key order.
    std::fprintf(
        f_,
        "{\"frame\":%d,\"screen\":%d,\"player_x\":%d,\"player_y\":%d,"
        "\"player_y_vel\":%d,\"facing_left\":%d,\"gravity_flag\":%d,"
        "\"walk_frame\":%d,\"club_flag\":%d,\"axe_flag\":%d,\"energy\":%d,"
        "\"lives\":%d,\"score\":%ld,\"food_count\":%d,\"death_flag\":%d,"
        "\"cave_flag\":%d,\"timer\":%d,\"frame_counter\":%d,"
        "\"cave_warp_freeze\":%d,\"sprite_queue_count\":%d}\n",
        frame, s.current_screen, s.player.x, s.player.y, s.player.y_vel,
        s.player.facing_left, s.player.gravity_flag, s.player.walk_frame,
        s.player.club_flag, s.player.axe_flag, s.player.energy,
        s.player.lives, s.score, s.food_count, s.player.death_counter,
        s.cave_flag, s.timer, s.frame_counter, s.player.cave_warp_freeze, 0);
}

bool InputRecorder::open(const std::string& path) {
    f_ = std::fopen(path.c_str(), "w");
    return f_ != nullptr;
}

InputRecorder::~InputRecorder() {
    if (f_ != nullptr) {
        std::fflush(f_);
        std::fclose(f_);
    }
}

void InputRecorder::record(int reader_frame, const systems::FrameInputs& in) {
    if (f_ == nullptr) return;
    // The reader's canonical key order + name list (InputReplay::at kKeys).
    // FrameInputs field mapping must match InputReplay::at exactly.
    static const char* kKeys[5] = {"left", "right", "up", "down", "attack"};
    const bool cur[5] = {in.left, in.right, in.up, in.down, in.attack};
    // Inverse of the reader's frame = time_ms / 55: time_ms = frame * 55
    // lands the event back on `reader_frame` (integer-exact, no rounding).
    const long time_ms = static_cast<long>(reader_frame) * 55;
    for (int i = 0; i < 5; ++i) {
        // First frame: any key already down is emitted as a press.
        if (!first_ && cur[i] == prev_[i]) continue;
        if (first_ && !cur[i]) continue;
        std::fprintf(f_, "{\"time_ms\":%ld,\"key\":\"%s\",\"action\":\"%s\"}\n",
                     time_ms, kKeys[i], cur[i] ? "press" : "release");
        prev_[i] = cur[i];
    }
    if (first_) {
        for (int i = 0; i < 5; ++i) prev_[i] = cur[i];
        first_ = false;
    }
}

void TraceWriter::write_boss(int frame, const systems::BossPlayerState& p,
                             int boss_health) {
    if (f_ == nullptr) return;
    // Boss-arena mapping (mirrors the reference implementation's boss-arena trace end-frame mapping):
    // screen / y_vel / gravity / axe / food / cave fields have no boss
    // equivalent and are 0; `energy` carries the boss health so club hits
    // are trace-visible; `timer` is the boss player's DS:0x9c66 slot.
    std::fprintf(
        f_,
        "{\"frame\":%d,\"screen\":0,\"player_x\":%d,\"player_y\":%d,"
        "\"player_y_vel\":0,\"facing_left\":%d,\"gravity_flag\":0,"
        "\"walk_frame\":%d,\"club_flag\":%d,\"axe_flag\":0,\"energy\":%d,"
        "\"lives\":%d,\"score\":%ld,\"food_count\":0,\"death_flag\":%d,"
        "\"cave_flag\":0,\"timer\":%d,\"frame_counter\":0,"
        "\"cave_warp_freeze\":0,\"sprite_queue_count\":0}\n",
        frame, p.x, p.y, p.facing_left ? 1 : 0, p.walk_frame, p.club_flag,
        boss_health, p.lives, p.score, p.death_counter, p.timer);
}

}  // namespace olduvai::presentation
