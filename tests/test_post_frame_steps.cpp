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
