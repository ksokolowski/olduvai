// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Spawner unit tests on synthetic records.  The full-coverage validation
// runs out-of-repo against the real executable: all 723 entities across
// every screen of every table spawn field-identical to the reference
// engine (45 fields each, LCG reseeded per screen) — verified 2026-06-09.

#include "doctest/doctest.h"
#include "core/constants.hpp"
#include "core/game_tables.hpp"
#include "core/rng.hpp"
#include "systems/spawning.hpp"

using namespace olduvai;
using namespace olduvai::core;
using namespace olduvai::systems;
using prepare::ObjectRecord;

namespace {
MonsterTables tables_with(int type, prepare::MonsterTable mt) {
    MonsterTables t;
    t.main[type] = mt;
    return t;
}
}  // namespace

TEST_CASE("egg/rock reset forces hit points regardless of spawn data") {
    MonsterTables none;
    const auto ents = spawn_screen_entities(
        {{0x03, {50, 60, 0}}, {0x04, {70, 80, 0}}, {0x1C, {90, 100, 0}}},
        none);
    REQUIRE(ents.size() == 3);
    CHECK(ents[0].counter == 2);    // egg
    CHECK(ents[1].counter == 3);    // rock
    CHECK(ents[2].counter == 2);    // breakable rock (shares the egg reset)
}

TEST_CASE("hidden food spawns dormant (state forced 2, invisible)") {
    MonsterTables none;
    const auto ents = spawn_screen_entities({{0x06, {10, 20, 1, 0}}}, none);
    CHECK(ents[0].state == 2);
    CHECK_FALSE(ents[0].visible);
    CHECK(ents[0].sprite == 60 + 1);
}

TEST_CASE("balloons record yields two entities") {
    MonsterTables none;
    const auto ents = spawn_screen_entities(
        {{0x08, {10, 20, 5, 30, 40, 9}}}, none);
    REQUIRE(ents.size() == 2);
    CHECK(ents[0].sprite == 4);   // 1-based → 0-based
    CHECK(ents[1].x == 30);
    CHECK(ents[1].sprite == 8);
}

TEST_CASE("fish consumes the LCG for its arc velocity") {
    global_rng().reseed(1);
    MonsterTables none;
    const auto ents = spawn_screen_entities({{0x09, {120, 0, 0, 0}}}, none);
    CHECK(ents[0].y == 220);
    // First LCG value from seed 1 is 0x015A = 346 → dy = 346 % 5 + 16 = 17.
    CHECK(ents[0].dy == 17);
    CHECK_FALSE(ents[0].visible);
}

TEST_CASE("monster pulls its data table; init_state pre-activates") {
    prepare::MonsterTable mt;
    mt.init_spr = 48; mt.move_spr = 50; mt.away_spr = 55; mt.ko_spr = 60;
    mt.energy = 2; mt.dat02 = 35; mt.var16 = 70; mt.di = 20; mt.si = 30;
    mt.direction_flag = 1;
    mt.walk_offsets = {0, 1, 2, 1, 0, 1, 2, 1};
    const auto tables = tables_with(0x0D, mt);
    const auto ents = spawn_screen_entities(
        {{0x0D, {3, 1, 200, 120, 0, 0, 0, 0, 1, 0, 0, 0}},   // init_state 3
         {0x0D, {0, 2, 100, 110, 0, 0, 0, 0, 0, 0, 0, 0}}},  // init_state 0
        tables);
    REQUIRE(ents.size() == 2);
    CHECK(ents[0].state == 3);          // pre-activated
    CHECK(ents[0].visible);
    CHECK(ents[0].hits_to_ko == 2);
    CHECK(ents[0].club_reach_right == 35);
    CHECK(ents[0].club_reach_left == 70);
    CHECK(ents[0].respawns == 1);
    CHECK(ents[1].state == static_cast<int>(MonsterState::Reset));
    CHECK_FALSE(ents[1].visible);
}

TEST_CASE("cave bat bounds come from the cave width table") {
    // Cave widths are runtime game data — install a synthetic table so the
    // test is self-contained (the real values come from the user's EXE).
    GameTables t;
    t.cave_sizes[5] = 256;
    install_game_tables(t);
    MonsterTables none;
    const auto ents = spawn_screen_entities({{0x17, {60, 70, 0, 0, 0}}},
                                            none, /*cave_index=*/5);
    CHECK(ents[0].y_top == 40);
    CHECK(ents[0].y_bottom == t.cave_sizes[5] - 8);
    install_game_tables(GameTables{});   // reset for other tests
}

TEST_CASE("fire record field order is x, state, y") {
    MonsterTables none;
    const auto ents = spawn_screen_entities({{0x13, {111, 7, 99}}}, none);
    CHECK(ents[0].x == 111);
    CHECK(ents[0].state == 7);
    CHECK(ents[0].y == 99);
}
