// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Headless unit tests for the score-tally countdown helpers.
// Tests the pure step_tally_bonus / step_tally_lives math extracted from
// show_score_tally (screens.cpp) — no SDL, no present() required.
//
// Reference: FUN_270a_01b4 (the reference implementation's score-tally).
// EXE pacing (FUN_270a_01b4 0x0303-0x03bd):
//   bonus countdown: bonus -= 2 (clamped to 0), score += 20 per step.
//   lives countdown: lives -= 1, score += 1000 per step.
//   Odd display levels award +1 life BEFORE the tally (tested by the
//   show_score_tally caller — not in these helpers).

#include "doctest/doctest.h"
#include "presentation/screens.hpp"

using namespace olduvai::presentation;

TEST_CASE("step_tally_bonus: basic decrement and score accumulation") {
    int bonus = 10;
    long score = 0;
    step_tally_bonus(bonus, score);
    CHECK(bonus == 8);
    CHECK(score == 20);
    step_tally_bonus(bonus, score);
    CHECK(bonus == 6);
    CHECK(score == 40);
}

TEST_CASE("step_tally_bonus: clamps at zero, never goes negative") {
    int bonus = 1;   // odd → would underflow without clamp
    long score = 0;
    step_tally_bonus(bonus, score);
    CHECK(bonus == 0);
    CHECK(score == 20);
    // Second call: already 0, stays 0.
    step_tally_bonus(bonus, score);
    CHECK(bonus == 0);
    CHECK(score == 40);
}

TEST_CASE("step_tally_lives: decrement and score accumulation") {
    int lives = 3;
    long score = 0;
    step_tally_lives(lives, score);
    CHECK(lives == 2);
    CHECK(score == 1000);
    step_tally_lives(lives, score);
    CHECK(lives == 1);
    CHECK(score == 2000);
    step_tally_lives(lives, score);
    CHECK(lives == 0);
    CHECK(score == 3000);
}

TEST_CASE("tally countdown: bonus 500 + 3 lives yields correct total score") {
    // Reference spec: bonus 500 → 250 steps of -2/+20 = +5000 score,
    // plus 3 lives × 1000 = +3000.  Total: 8000.
    int bonus = 500;
    int lives = 3;
    long score = 0;

    // Bonus phase.
    while (bonus > 0) {
        step_tally_bonus(bonus, score);
    }
    CHECK(bonus == 0);
    CHECK(score == 5000);   // 250 steps × 20

    // Lives phase.
    while (lives > 0) {
        step_tally_lives(lives, score);
    }
    CHECK(lives == 0);
    CHECK(score == 8000);   // 5000 + 3×1000
}

TEST_CASE("tally countdown: bonus 500 step count is exactly 250") {
    int bonus = 500;
    long score = 0;
    int steps = 0;
    while (bonus > 0) {
        step_tally_bonus(bonus, score);
        ++steps;
    }
    CHECK(steps == 250);
}
