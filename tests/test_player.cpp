// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Player physics parity — a 70-frame scripted scenario (idle, walk-cycle,
// jump arc, landing, attack latch, club-walk, lethal hit, death animation,
// ghost float) pinned frame-by-frame against the validated reference
// engine (sequence dumped 2026-06-09).

#include <array>

#include "doctest/doctest.h"
#include "systems/player.hpp"

using namespace olduvai;
using namespace olduvai::systems;

namespace {

struct Frame {
    int x, y, sprite, gravity_flag, club_flag, energy, death_counter,
        lives, facing_left, hit_counter;
};

constexpr std::array<Frame, 70> kOracle = {{
    {40, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {40, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {40, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {40, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {40, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {46, 129, 1, 0, 0, 10, 0, 3, 0, 0},
    {52, 129, 96, 0, 0, 10, 0, 3, 0, 0},
    {56, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {62, 129, 2, 0, 0, 10, 0, 3, 0, 0},
    {68, 129, 97, 0, 0, 10, 0, 3, 0, 0},
    {72, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {78, 129, 1, 0, 0, 10, 0, 3, 0, 0},
    {84, 129, 96, 0, 0, 10, 0, 3, 0, 0},
    {88, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {94, 129, 2, 0, 0, 10, 0, 3, 0, 0},
    {100, 129, 97, 1, 0, 10, 0, 3, 0, 0},
    {107, 121, 3, 5, 0, 10, 0, 3, 0, 0},
    {114, 113, 3, 9, 0, 10, 0, 3, 0, 0},
    {121, 104, 3, 13, 0, 10, 0, 3, 0, 0},
    {128, 100, 3, 17, 0, 10, 0, 3, 0, 0},
    {135, 98, 3, 0, 0, 10, 0, 3, 0, 0},
    {140, 108, 3, 0, 0, 10, 0, 3, 0, 0},
    {145, 118, 3, 0, 0, 10, 0, 3, 0, 0},
    {150, 128, 3, 0, 0, 10, 0, 3, 0, 0},
    {155, 129, 3, 0, 0, 10, 0, 3, 0, 0},
    {159, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {165, 129, 1, 0, 0, 10, 0, 3, 0, 0},
    {171, 129, 96, 0, 0, 10, 0, 3, 0, 0},
    {175, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {181, 129, 2, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 97, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 0, 0, 0, 10, 0, 3, 0, 0},
    {187, 129, 4, 0, 2, 10, 0, 3, 0, 0},
    {187, 129, 4, 0, 2, 10, 0, 3, 0, 0},
    {187, 129, 4, 0, 2, 10, 0, 3, 0, 0},
    {187, 129, 4, 0, 2, 10, 0, 3, 0, 0},
    {182, 129, 4, 0, 2, 10, 0, 3, 1, 0},
    {177, 129, 4, 0, 2, 10, 0, 3, 1, 0},
    {172, 129, 4, 0, 2, 10, 0, 3, 1, 0},
    {167, 129, 4, 0, 2, 10, 0, 3, 1, 0},
    {162, 129, 4, 0, 2, 10, 0, 3, 1, 0},
    {162, 129, 7, 0, 2, 0, 2, 3, 1, 0},
    {162, 129, 7, 0, 2, 0, 3, 3, 1, 0},
    {162, 129, 100, 0, 2, 0, 4, 3, 1, 0},
    {162, 129, 101, 0, 2, 0, 5, 3, 1, 0},
    {162, 129, 102, 0, 2, 0, 6, 3, 1, 0},
    {162, 129, 103, 0, 2, 0, 7, 3, 1, 0},
    {162, 129, 103, 0, 2, 0, 8, 3, 1, 0},
    {162, 129, 103, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 39, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 38, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 39, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 38, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 39, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 38, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 39, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 38, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 39, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 38, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 39, 0, 2, 0, 9, 3, 1, 0},
    {162, 129, 38, 0, 2, 0, 9, 3, 1, 0},
}};

}  // namespace

TEST_CASE("70-frame scripted scenario matches the reference engine") {
    SystemsState st;
    for (int x = 0; x < 320; ++x) st.collision.set_bit(x, 158);
    st.player.x = 40;
    st.player.y = 129;
    st.player.restart_x = 40;
    st.player.restart_y = 129;
    st.player.hit_counter = 0;  // skip spawn invulnerability for clarity

    for (int f = 0; f < 70; ++f) {
        InputState& i = st.input;
        i = {};
        if (f >= 5 && f <= 14) i.right = true;
        if (f == 15) { i.jump = true; i.right = true; }
        if (f >= 16 && f <= 30) i.right = true;
        if (f == 41) i.attack = true;
        if (f >= 45 && f <= 49) i.left = true;
        if (f == 50) hit_player(st, 99);

        tick_post_hit_invuln(st.player);
        update_player(st);
        clamp_player_position(st);
        check_death_by_fall(st);

        const Frame& e = kOracle[static_cast<std::size_t>(f)];
        const PlayerState& p = st.player;
        INFO("frame ", f);
        CHECK(p.x == e.x);
        CHECK(p.y == e.y);
        CHECK(p.sprite == e.sprite);
        CHECK(p.gravity_flag == e.gravity_flag);
        CHECK(p.club_flag == e.club_flag);
        CHECK(p.energy == e.energy);
        CHECK(p.death_counter == e.death_counter);
        CHECK(p.lives == e.lives);
        CHECK(p.facing_left == e.facing_left);
        CHECK(p.hit_counter == e.hit_counter);
    }
    CHECK_FALSE(st.game_over);
}

TEST_CASE("ghost float completes into respawn with full state reset") {
    SystemsState st;
    for (int x = 0; x < 320; ++x) st.collision.set_bit(x, 158);
    st.player.x = 100;
    st.player.y = 129;
    st.player.restart_x = 100;
    st.player.restart_y = 129;
    st.player.hit_counter = 0;
    st.timer = 5;  // exercise the timer recovery (+10 when < 11)

    trigger_death(st);
    int frames = 0;
    while (st.player.death_counter != 0 && frames < 200) {
        st.input = {};
        update_player(st);
        ++frames;
    }
    CHECK(st.player.death_counter == 0);
    CHECK(st.player.lives == 2);
    CHECK(st.player.energy == core::kInitialEnergy);
    CHECK(st.player.hit_counter == 0x27);   // respawn invulnerability
    CHECK(st.player.x == 100);
    CHECK(st.player.climbing == 0);
    CHECK(st.halo_flight_flag == 0);        // power-ups dropped
    CHECK(st.timer == 15);                  // 5 + 10
    CHECK_FALSE(st.game_over);
}

TEST_CASE("attack during ghost float skips to respawn") {
    SystemsState st;
    for (int x = 0; x < 320; ++x) st.collision.set_bit(x, 158);
    st.player.x = 100; st.player.y = 129;
    st.player.restart_x = 100; st.player.restart_y = 129;
    st.player.hit_counter = 0;
    trigger_death(st);
    for (int f = 0; f < 9; ++f) { st.input = {}; update_player(st); }
    CHECK(st.player.death_counter == 9);    // animation done, floating
    st.input = {};
    st.input.attack = true;                  // skip
    update_player(st);
    st.input = {};
    update_player(st);
    CHECK(st.player.death_counter == 0);    // respawned
    CHECK(st.player.lives == 2);
}

TEST_CASE("out of lives sets game_over") {
    SystemsState st;
    st.player.lives = 0;
    respawn(st);
    CHECK(st.game_over);
    CHECK(st.player.lives == -1);
}

TEST_CASE("sub-lethal hit applies cooldown; guard blocks repeat hits") {
    SystemsState st;
    st.player.hit_counter = 0;
    hit_player(st, 2);
    CHECK(st.player.energy == 8);
    CHECK(st.player.hit_counter == 0x13);
    hit_player(st, 2);                       // guarded — no double dip
    CHECK(st.player.energy == 8);
}

TEST_CASE("cave warp freeze suppresses the lethal path") {
    SystemsState st;
    st.player.hit_counter = 0;
    st.player.energy = 1;
    st.player.cave_warp_freeze = 4;
    hit_player(st, 5);
    CHECK(st.player.death_counter == 0);     // suppressed
    CHECK(st.player.energy == 1);
}

TEST_CASE("respawn into saved cave restores mode + screen-change") {
    SystemsState st;
    st.player.lives = 3;
    st.player.restart_screen = 5;
    st.player.restart_cave_index = 2;
    st.current_screen = 5;                    // same screen…
    st.cave_flag = 0;                         // …but mode flipped
    respawn(st);
    CHECK(st.cave_flag == 1);
    CHECK(st.cave_index == 2);
    CHECK(st.screen_change);                  // collision must rebuild
}

TEST_CASE("god + flight: drowning is clamped to waterline, no death loop") {
    SystemsState st;
    st.god_mode = true;
    st.glider_active = true;       // balloon/glider flight
    st.player.y = 0xB4 + 30;       // dove into the water
    check_death_by_fall(st);
    CHECK(st.player.death_counter == 0);  // no death started
    CHECK(st.player.y == 0xB4);           // held at the waterline, recoverable
}

TEST_CASE("god without flight: normal ground fall still triggers respawn") {
    SystemsState st;
    st.god_mode = true;
    st.glider_active = false;      // on foot
    st.player.y = 0xB4 + 30;
    check_death_by_fall(st);
    CHECK(st.player.death_counter == 1);  // guard must not leak to ground falls
}
