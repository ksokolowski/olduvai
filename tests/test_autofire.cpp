// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Autofire speed levels — token→cooldown map, the Autofire pacing struct's
// release-reset behaviour, and cadence through the real update_player latch
// with the weapon-draw decrement simulated (headless: game_render.cpp owns
// the decrement in the live engine).

#include <string>

#include "doctest/doctest.h"
#include "presentation/autofire.hpp"
#include "systems/player.hpp"

using namespace olduvai;
using namespace olduvai::systems;
using olduvai::presentation::Autofire;
using olduvai::presentation::autofire_cooldown;

TEST_CASE("autofire_cooldown token map") {
    CHECK(autofire_cooldown("fast") == 0);
    CHECK(autofire_cooldown("medium") == 2);
    CHECK(autofire_cooldown("slow") == 4);
    CHECK(autofire_cooldown("off") == -1);
    CHECK(autofire_cooldown("") == -1);
    CHECK(autofire_cooldown("true") == -1);   // bool-era value → off
}

TEST_CASE("off passes the raw held state through") {
    Autofire af;   // cooldown -1
    CHECK(af.attack(true, 0, 0) == true);
    CHECK(af.attack(true, 2, 1) == true);
    CHECK(af.attack(false, 0, 0) == false);
}

TEST_CASE("release resets pacing credit") {
    Autofire af{2, 0};               // medium
    CHECK(!af.attack(true, 0, 0));   // idle 1
    CHECK(!af.attack(true, 0, 0));   // idle 2
    CHECK(!af.attack(false, 0, 0));  // release → reset
    CHECK(!af.attack(true, 0, 0));   // idle 1 again (no stale credit)
    CHECK(!af.attack(true, 0, 0));   // idle 2
    CHECK(af.attack(true, 0, 0));    // idle 3 > 2 → press
}

namespace {

// One headless frame: synthesize, update, then mirror the weapon-overlay
// draw decrement.  Returns true when a new swing started this frame.
bool run_frame(SystemsState& st, Autofire& af) {
    st.input = {};
    st.input.attack = af.attack(/*held=*/true, st.player.club_flag,
                                st.player.attack_latch);
    const int before = st.player.club_flag;
    update_player(st);
    const bool swung = before == 0 && st.player.club_flag == 2;
    if (st.player.club_flag > 0) --st.player.club_flag;
    return swung;
}

SystemsState grounded_state() {
    SystemsState st;
    for (int x = 0; x < 320; ++x) st.collision.set_bit(x, 158);
    st.player.x = 40;
    st.player.y = 129;
    st.player.restart_x = 40;
    st.player.restart_y = 129;
    st.player.hit_counter = 0;  // skip spawn invulnerability
    return st;
}

int swings_in_24_frames(const std::string& token) {
    SystemsState st = grounded_state();
    Autofire af{autofire_cooldown(token), 0};
    int swings = 0;
    for (int f = 0; f < 24; ++f)
        if (run_frame(st, af)) ++swings;
    return swings;
}

}  // namespace

TEST_CASE("cadence per speed through the real latch") {
    CHECK(swings_in_24_frames("fast") == 12);    // period 2 (perfect mash)
    CHECK(swings_in_24_frames("medium") == 6);   // period 4
    CHECK(swings_in_24_frames("slow") == 4);     // period 6
    CHECK(swings_in_24_frames("off") == 1);      // raw hold: latch blocks
}
