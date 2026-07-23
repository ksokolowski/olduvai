// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/score.hpp"

#include <algorithm>   // std::min

namespace olduvai::systems {

namespace {
constexpr int kScoreValues[5] = {10, 50, 100, 200, 500};
}

void add_score(SystemsState& state, int amount) {
    state.score += amount;
    if (state.score > kScoreCap) state.score = kScoreCap;  // 999,999 cap
    while (state.score >= state.next_life_score) {         // life per 10K
        state.player.lives = std::min(state.player.lives + 1, 99);
        state.next_life_score += 10000;
    }
    state.sfx_generic_pending = true;   // score "ding"
}

void add_score_popup(SystemsState& state, int x, int y, int score) {
    int idx = -1;
    for (int i = 0; i < 5; ++i) {
        if (kScoreValues[i] == score) { idx = i; break; }
    }
    if (idx < 0) return;   // value has no popup sprite
    for (auto& b : state.score_bonuses) {
        if (b.counter == 0) {
            b.counter = kScorePopupFrames;
            b.x = x;
            b.y = y;
            b.sprite = kScoreSpriteBase + idx;
            b.active_this_frame = false;
            return;
        }
    }
}

void update_score_bonuses(SystemsState& state) {
    for (auto& b : state.score_bonuses) {
        if (b.counter > 0) {
            b.active_this_frame = true;
            --b.counter;
        } else {
            b.active_this_frame = false;
        }
    }
}

void move_score_bonuses(SystemsState& state) {
    // y moves AFTER the draw (draw-before-move ordering).
    for (auto& b : state.score_bonuses) {
        if (b.active_this_frame) b.y += kScorePopupDy;
    }
}

}  // namespace olduvai::systems
