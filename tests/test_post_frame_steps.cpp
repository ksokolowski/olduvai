// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Post-frame steps 6b-8a — the dispatch contract of
// systems::run_post_frame_steps (docs/FRAME_LOOP.md).
//
// WHY these exist at all: until the block moved out of run_platform_level it
// could only be reached through the shell, so the only things covering it were
// golden_trace and boss_golden_trace — both asset-gated, so **neither runs in
// CI**. Post-frame gameplay was therefore proven on exactly one machine. These
// are always-green and need no game files.
//
// What they pin is the ORDER AND GATING, not the individual steps: which
// branch runs, what excludes what, and what is deliberately allowed to run in
// a state where the player update returns early. Every one of those is a
// documented cross-engine divergence risk. The steps' own behaviour is
// covered by test_transitions / test_player / test_collisions.

#include "doctest/doctest.h"

#include "systems/frame_runner.hpp"
#include "systems/player.hpp"
#include "systems/transitions.hpp"   // kFoodGate

using olduvai::systems::SystemsState;
using olduvai::systems::run_post_frame_steps;

namespace {

// A state that will not trip any transition: mid-screen, nothing flagged.
SystemsState quiet_state() {
    SystemsState s;
    s.current_level = 1;
    s.current_screen = 2;
    s.player.x = 160;
    s.player.y = 100;
    return s;
}

}  // namespace

TEST_CASE("post-frame 6b: the death-halo branch is chosen by death_counter") {
    SUBCASE("counter == 1 initialises the halo — but only in flight") {
        // init_death_halo's own gate: the halo is a balloon (L1) / glider (L5)
        // death only. Dying on foot must not raise it.
        SystemsState s = quiet_state();
        s.player.death_counter = 1;
        s.glider_active = true;
        s.current_level = 1;
        run_post_frame_steps(s);
        CHECK(s.death_halo_active == true);
    }
    SUBCASE("counter == 1 on foot leaves it clear") {
        SystemsState s = quiet_state();
        s.player.death_counter = 1;
        s.glider_active = false;
        run_post_frame_steps(s);
        CHECK(s.death_halo_active == false);
    }
    SUBCASE("counter == 0 clears it — the else branch is not a no-op") {
        SystemsState s = quiet_state();
        s.player.death_counter = 0;
        s.death_halo_active = true;      // left over from a previous death
        run_post_frame_steps(s);
        CHECK(s.death_halo_active == false);
    }
}

TEST_CASE("post-frame 6e: the clamp is skipped on a screen change") {
    // The clamp pulls x<0 back to 0. On the frame a screen change is already
    // pending it must NOT, or the player is dragged back off the edge that
    // triggered the transition.
    //
    // Screen 0 is deliberate: it isolates the clamp from step 8. The left-edge
    // transition needs `scr > 0`, so on screen 0 nothing else in this block
    // touches x and the clamp is the only candidate.
    SystemsState s = quiet_state();
    s.current_screen = 0;
    s.player.x = -8;
    s.screen_change = true;
    run_post_frame_steps(s);
    CHECK(s.player.x == -8);             // skipped

    SystemsState t = quiet_state();
    t.current_screen = 0;
    t.player.x = -8;
    t.screen_change = false;
    run_post_frame_steps(t);
    CHECK(t.player.x == 0);              // clamped
}

TEST_CASE("post-frame 7: cave and secret are mutually exclusive paths") {
    // In a cave the spring flag is force-cleared and the secret trampoline
    // never runs; in a secret room the trampoline drives the flag instead.
    SystemsState s = quiet_state();
    s.cave_flag = 1;
    s.secret_spring_bouncing = true;
    run_post_frame_steps(s);
    CHECK(s.secret_spring_bouncing == false);
}

TEST_CASE("post-frame 8: surface transitions run only outside cave and secret") {
    // The gate is `!cave_flag && !secret_flag`. Inside either, the surface
    // transition check must not fire — a cave exit is step 7's job.
    SystemsState s = quiet_state();
    s.cave_flag = 1;
    s.screen_change = false;
    s.player.x = 316;                    // hard against the right seam
    run_post_frame_steps(s);
    CHECK(s.screen_change == false);     // no surface transition inside a cave
}

TEST_CASE("post-frame: the whole block is a no-op on a quiet mid-screen state") {
    // The guard against a step firing unconditionally: nothing here may run
    // just because the function was called.
    SystemsState s = quiet_state();
    const int x = s.player.x, y = s.player.y;
    run_post_frame_steps(s);
    CHECK(s.player.x == x);
    CHECK(s.player.y == y);
    CHECK(s.screen_change == false);
    CHECK(s.death_halo_active == false);
    CHECK(s.cave_flag == 0);
    CHECK(s.secret_flag == 0);
}

// ── The shell's tick around run_frame (systems::wrap_frame_counter,
// run_tick, end_tick), moved out of run_platform_level. ──────────────────────

TEST_CASE("wrap_frame_counter: past 0x3D it resets and takes a timer tick") {
    SystemsState s = quiet_state();
    s.timer = 10;
    s.frame_counter = 0x3D;
    olduvai::systems::wrap_frame_counter(s, /*god=*/false);
    CHECK(s.frame_counter == 0x3D);   // not past it yet
    CHECK(s.timer == 10);
    s.frame_counter = 0x3E;
    olduvai::systems::wrap_frame_counter(s, false);
    CHECK(s.frame_counter == 0);
    CHECK(s.timer == 9);
}

TEST_CASE("wrap_frame_counter: an empty timer kills, or refills under --god") {
    SystemsState s = quiet_state();
    s.timer = 0;
    s.frame_counter = 0x3E;
    olduvai::systems::wrap_frame_counter(s, /*god=*/true);
    CHECK(s.timer == 99);
    CHECK(s.player.death_counter == 0);

    SystemsState d = quiet_state();
    d.timer = 0;
    d.frame_counter = 0x3E;
    olduvai::systems::wrap_frame_counter(d, /*god=*/false);
    CHECK(d.player.death_counter != 0);
}

TEST_CASE("run_tick paused: no run_frame (the frame counter holds)") {
    SystemsState s = quiet_state();
    s.frame_counter = 5;
    olduvai::systems::FrameInputs in;
    in.right = true;
    olduvai::systems::run_tick(s, in, /*paused=*/true);
    CHECK(s.frame_counter == 5);
    CHECK(s.input.right);   // the inputs still land
    olduvai::systems::run_tick(s, in, /*paused=*/false);
    CHECK(s.frame_counter == 6);
}

TEST_CASE("end_tick: --god tops up and masks game over before the steps") {
    SystemsState s = quiet_state();
    s.player.energy = 1;
    s.player.lives = 0;
    s.food_count = 3;
    s.game_over = true;
    olduvai::systems::end_tick(s, /*god=*/true);
    CHECK(s.player.energy == 999);
    CHECK(s.player.lives == 99);
    CHECK(s.food_count == olduvai::systems::kFoodGate);
    CHECK_FALSE(s.game_over);

    SystemsState n = quiet_state();
    n.player.energy = 1;
    olduvai::systems::end_tick(n, /*god=*/false);
    CHECK(n.player.energy == 1);
}

TEST_CASE("tick_teleport_fx: the departure drains before the arrival") {
    SystemsState s = quiet_state();
    s.teleport_out_ticks = 1;
    s.teleport_in_ticks = 2;
    olduvai::systems::tick_teleport_fx(s);
    CHECK(s.teleport_out_ticks == 0);
    CHECK(s.teleport_in_ticks == 2);
    olduvai::systems::tick_teleport_fx(s);
    CHECK(s.teleport_in_ticks == 1);
}

TEST_CASE("tick_get_ready: even frames inside [2,17] only") {
    SystemsState s = quiet_state();
    s.get_ready_counter = 17;
    s.frame_counter = 3;
    olduvai::systems::tick_get_ready(s);
    CHECK(s.get_ready_counter == 17);
    s.frame_counter = 4;
    olduvai::systems::tick_get_ready(s);
    CHECK(s.get_ready_counter == 16);
    s.get_ready_counter = 0x11 + 1;   // above the window: held
    olduvai::systems::tick_get_ready(s);
    CHECK(s.get_ready_counter == 0x12);
    s.get_ready_counter = 1;          // below it: held
    olduvai::systems::tick_get_ready(s);
    CHECK(s.get_ready_counter == 1);
}

TEST_CASE("tick_cave_emerge: counts down to zero and stops") {
    SystemsState s = quiet_state();
    s.cave_emerge_frames = 1;
    olduvai::systems::tick_cave_emerge(s);
    CHECK(s.cave_emerge_frames == 0);
    olduvai::systems::tick_cave_emerge(s);
    CHECK(s.cave_emerge_frames == 0);
}
