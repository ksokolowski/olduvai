// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Tests for systems/transitions — L3 trunk-descent smoke jitter roll.
//
// Parity test: seed the native LCG to a known state, roll 20 pairs via
// roll_l3_descent_smoke_jitter, assert the pairs AND the post-roll rng
// state exactly equal values produced by running the Python reference:
//
//   # using the reference implementation's RandLCG
//   rng = RandLCG(seed=1)
//   pairs = [(rng.next()%10, rng.next()%10) for _ in range(20)]
//   # post-roll state: rng.state == 0xBEDC6C79
//
// The Python reference uses the same recurrence:
//   state = state * 0x015A4E35 + 1  (mod 2^32)
//   result = (state >> 16) & 0x7FFF
// and the same modulus 10 for each draw (FUN_2276_03d9:0x0554/0x0586).
// Values below were produced by running the command above 2026-06-12 and
// hard-coded to lock the stream.

#include "doctest/doctest.h"
#include "core/constants.hpp"
#include "core/rng.hpp"
#include "systems/cave_logic.hpp"
#include "systems/frame_runner.hpp"
#include "systems/player.hpp"
#include "systems/transitions.hpp"

using olduvai::core::RandLcg16;
using olduvai::core::global_rng;
using olduvai::systems::SystemsState;
using olduvai::systems::roll_l3_descent_smoke_jitter;

// Reference values produced by:
//   # using the reference implementation's RandLCG
//   rng = RandLCG(seed=1)
//   pairs = [(rng.next()%10, rng.next()%10) for _ in range(20)]
//   final_state = rng.state   # 0xBEDC6C79
//
// Pair layout: (jitter_a % 10, jitter_b % 10) per outer iteration.
// jitter_a = smoke A Y offset, jitter_b = smoke B Y offset.
// FUN_2276_03d9:0x0554 (smoke A lcall) + 0x0586 (smoke B lcall).
static constexpr std::pair<int,int> kExpectedPairs[20] = {
    {6, 0},  // iter  0
    {2, 0},  // iter  1
    {6, 7},  // iter  2
    {5, 5},  // iter  3
    {8, 6},  // iter  4
    {4, 8},  // iter  5
    {1, 9},  // iter  6
    {2, 0},  // iter  7
    {2, 1},  // iter  8
    {3, 7},  // iter  9
    {9, 1},  // iter 10
    {0, 5},  // iter 11
    {4, 9},  // iter 12
    {2, 1},  // iter 13
    {9, 6},  // iter 14
    {1, 1},  // iter 15
    {5, 3},  // iter 16
    {4, 0},  // iter 17
    {9, 5},  // iter 18
    {1, 2},  // iter 19
};
// LCG state after 40 draws from seed=1 (verified against Python reference).
static constexpr std::uint32_t kExpectedFinalState = 0xBEDC6C79u;

TEST_CASE("L3 descent smoke jitter: 20 pairs from seed=1 match Python reference") {
    // Seed the shared global LCG to the known initial state (= EXE boot value).
    global_rng().reseed(1u);

    SystemsState state;
    roll_l3_descent_smoke_jitter(state);

    // Verify all 20 pairs.
    for (int i = 0; i < 20; ++i) {
        INFO("iter ", i);
        CHECK(state.l3_descent_smoke_jitter[static_cast<std::size_t>(i)].first
              == kExpectedPairs[i].first);
        CHECK(state.l3_descent_smoke_jitter[static_cast<std::size_t>(i)].second
              == kExpectedPairs[i].second);
    }

    // Verify post-roll LCG state (ensures exactly 40 draws were consumed,
    // not fewer or more — any extra draw would fork subsequent RNG events).
    CHECK(global_rng().state() == kExpectedFinalState);
}

TEST_CASE("L3 descent smoke jitter: exactly 40 LCG draws consumed") {
    // Independent check: seed to 1, manually advance 40 draws, verify
    // the state matches what roll_l3_descent_smoke_jitter leaves behind.
    RandLcg16 manual(1u);
    for (int i = 0; i < 40; ++i) manual.next();
    CHECK(manual.state() == kExpectedFinalState);
}

TEST_CASE("L3 descent smoke jitter: smoke-A draw precedes smoke-B per iter") {
    // Verify the a-before-b ordering matches EXE (0x0554 lcall before 0x0586).
    // Seed to a value that makes a != b detectable.
    global_rng().reseed(42u);
    // Snapshot the sequence manually.
    RandLcg16 ref(42u);
    std::pair<int,int> manual_pairs[20];
    for (auto& p : manual_pairs) {
        p.first  = static_cast<int>(ref.next() % 10);
        p.second = static_cast<int>(ref.next() % 10);
    }

    global_rng().reseed(42u);
    SystemsState state;
    roll_l3_descent_smoke_jitter(state);

    for (int i = 0; i < 20; ++i) {
        INFO("iter ", i);
        CHECK(state.l3_descent_smoke_jitter[static_cast<std::size_t>(i)].first
              == manual_pairs[i].first);
        CHECK(state.l3_descent_smoke_jitter[static_cast<std::size_t>(i)].second
              == manual_pairs[i].second);
    }
}

// Cave-warp fade signal (presentation-only).  The L3 11→12 trunk-cave EXIT and
// the L7-style screen-9 cave ENTRY route through the EXE's bp-6=0 →
// Sprite_DrawDispatch mode=2 fade.  These
// paths set screen_change directly and never touch cave_warp_freeze, so the
// fade is signalled via cave_warp_pending.  Mirrors the reference
// implementation's transition and cave-logic paths.
TEST_CASE("L3 11->12 trunk-cave exit raises cave_warp_pending (fade signal)") {
    using olduvai::systems::check_l3_transition;
    SystemsState state;
    state.current_level = 3;
    state.current_screen = 11;
    state.screen_change = false;
    // Climb-out gate: y < 0x87 && x < 0x5A.  Keep y <= 0x22 so the earlier
    // scr==11 clamp (x<100 && y>0x22 → x=100) doesn't push x out of the gate.
    state.player.x = 0x40;
    state.player.y = 0x20;

    check_l3_transition(state);

    CHECK(state.current_screen == 12);        // climbed out
    CHECK(state.screen_change == true);
    CHECK(state.player.cave_warp_pending == true);   // fade selected
    // Must NOT abuse the freeze counter (>0 freezes the player).
    CHECK(state.player.cave_warp_freeze == 0);
}

TEST_CASE("L7-style screen-9 cave entry raises cave_warp_pending (fade signal)") {
    using olduvai::core::kCaveTransitionMarker;
    using olduvai::systems::enter_cave;
    SystemsState state;
    state.current_level = 7;
    state.current_screen = 9;

    enter_cave(state, kCaveTransitionMarker);

    CHECK(state.current_screen == 10);        // warped
    CHECK(state.screen_change == true);
    CHECK(state.player.cave_warp_pending == true);   // fade selected
    CHECK(state.player.cave_warp_freeze == 0);       // not via freeze counter
}

// ── Cave-EMERGE reveal (Enhanced #18 v2) — pacing arm + freeze/cancel ──
// Enhanced arms 9 ticks (3 stages × 3-tick holds, frozen); classic arms
// 2 draw-only ticks (EXE gameplay timing untouched).  A real hit or a
// death during the frozen ticks cancels the emerge cleanly.

TEST_CASE("exit_cave arms the emerge: 9 ticks enhanced, 2 classic") {
    using olduvai::systems::exit_cave;
    using olduvai::systems::kCaveEmergeTicksClassic;
    using olduvai::systems::kCaveEmergeTicksEnhanced;
    SystemsState state;
    state.cave_flag = 1;
    state.cave_return_screen = 1;
    state.cave_return_x = 180;
    state.cave_return_y = 126;

    SUBCASE("enhanced") {
        state.enhanced_active = true;
        exit_cave(state);
        CHECK(state.cave_emerge_frames == kCaveEmergeTicksEnhanced);
        CHECK(state.cave_emerge_frames == 9);
    }
    SUBCASE("classic") {
        state.enhanced_active = false;
        exit_cave(state);
        CHECK(state.cave_emerge_frames == kCaveEmergeTicksClassic);
        CHECK(state.cave_emerge_frames == 2);
    }
}

TEST_CASE("a real hit during the enhanced emerge cancels it cleanly") {
    using olduvai::systems::hit_player;
    SystemsState state;
    state.enhanced_active = true;
    state.cave_emerge_frames = 7;
    state.player.energy = 10;
    state.player.hit_counter = 0;
    state.player.death_counter = 0;

    SUBCASE("non-lethal hit clears the counter") {
        hit_player(state, 2);
        CHECK(state.player.energy == 8);
        CHECK(state.cave_emerge_frames == 0);
    }
    SUBCASE("EXE entry guard: stale invulnerability does NOT cancel") {
        state.player.hit_counter = 5;   // pre-exit hit still cooling down
        hit_player(state, 2);
        CHECK(state.player.energy == 10);       // guard rejected the hit
        CHECK(state.cave_emerge_frames == 7);   // emerge untouched
    }
    SUBCASE("classic emerge is not touched by hits (draw-only)") {
        state.enhanced_active = false;
        state.cave_emerge_frames = 2;
        hit_player(state, 2);
        CHECK(state.cave_emerge_frames == 2);
    }
}

TEST_CASE("enhanced emerge freezes the player; classic does not") {
    using olduvai::systems::FrameInputs;
    using olduvai::systems::run_frame;
    SystemsState state;
    state.current_level = 1;
    state.current_screen = 1;
    state.player.x = 180;
    state.player.y = 126;
    state.player.energy = 10;
    FrameInputs in;
    in.right = true;

    SUBCASE("enhanced: frozen for the whole reveal (stop game time)") {
        state.enhanced_active = true;
        state.cave_emerge_frames = 9;
        for (int i = 0; i < 9; ++i) run_frame(state, in);
        CHECK(state.player.x == 180);
        CHECK(state.player.y == 126);
        // The counter itself is presentation-owned (game_app end-of-tick
        // decrement), so run_frame must NOT consume it.
        CHECK(state.cave_emerge_frames == 9);
    }
    SUBCASE("classic: draw-only — gameplay unaffected (EXE timing)") {
        state.enhanced_active = false;
        state.cave_emerge_frames = 2;
        run_frame(state, in);
        const bool moved =
            state.player.x != 180 || state.player.y != 126;
        CHECK(moved);
    }
    SUBCASE("death during the frozen ticks cancels the emerge") {
        state.enhanced_active = true;
        state.cave_emerge_frames = 5;
        state.player.death_counter = 1;
        run_frame(state, in);
        CHECK(state.cave_emerge_frames == 0);
    }
}
