// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Entity-update parity (batch 1) — a 56-frame scripted scenario driving a
// shared-state-machine monster (reset → spawn → heading walk → club-hit →
// running-away → recover → bomb-KO → body-collect → dead with respawns
// cleared), a fish jump arc, and an oscillating platform — pinned
// frame-by-frame against the validated reference engine (2026-06-09).

#include <array>

#include "doctest/doctest.h"
#include "systems/monster_ai.hpp"

using namespace olduvai;
using namespace olduvai::core;
using namespace olduvai::systems;

namespace {

struct Row {
    int mx, my, mstate, mspr, mdir, msc, mko, mvis, mact, mres;
    int fy, fdy, fstate, fspr, fvis;
    int pcy, pdir, py;
    int clear, l3a;
};

constexpr std::array<Row, 56> kOracle = {{
    {200, 120, 1, 48, 1, 0, 0, 1, 1, 1, 200, 19, 0, 83, 0, 97, 0, 100, 0, 1},
    {200, 120, 1, 48, 1, 1, 0, 1, 1, 1, 181, 18, 0, 83, 1, 94, 0, 97, 0, 1},
    {200, 120, 3, 49, 1, 0, 0, 1, 1, 1, 163, 17, 0, 83, 1, 91, 0, 94, 0, 2},
    {200, 120, 3, 50, 1, 0, 0, 1, 1, 1, 146, 16, 0, 83, 1, 88, 1, 91, 0, 2},
    {196, 120, 3, 51, 1, 1, 0, 1, 1, 1, 130, 15, 0, 83, 1, 91, 1, 88, 0, 3},
    {196, 120, 3, 51, 1, 1, 0, 1, 1, 1, 115, 14, 0, 83, 1, 94, 1, 91, 0, 3},
    {192, 120, 3, 52, 1, 2, 0, 1, 1, 1, 101, 13, 0, 83, 1, 97, 1, 94, 0, 4},
    {192, 120, 3, 52, 1, 2, 0, 1, 1, 1, 88, 12, 0, 83, 1, 100, 1, 97, 0, 4},
    {188, 120, 3, 51, 1, 3, 0, 1, 1, 1, 76, 11, 0, 83, 1, 103, 1, 100, 0, 5},
    {188, 120, 3, 51, 1, 3, 0, 1, 1, 1, 65, 10, 0, 83, 1, 106, 1, 103, 0, 5},
    {184, 120, 3, 50, 1, 4, 0, 1, 1, 1, 55, 9, 0, 83, 1, 109, 1, 106, 0, 6},
    {184, 120, 3, 50, 1, 4, 0, 1, 1, 1, 46, 8, 0, 83, 1, 112, 0, 109, 0, 6},
    {180, 120, 3, 51, 1, 5, 0, 1, 1, 1, 38, 7, 0, 83, 1, 109, 0, 112, 0, 7},
    {180, 120, 3, 51, 1, 5, 0, 1, 1, 1, 31, 6, 0, 83, 1, 106, 0, 109, 0, 7},
    {176, 120, 3, 52, 1, 6, 0, 1, 1, 1, 25, 5, 0, 83, 1, 103, 0, 106, 0, 8},
    {176, 120, 3, 52, 1, 6, 0, 1, 1, 1, 20, 4, 0, 83, 1, 100, 0, 103, 0, 8},
    {172, 120, 3, 51, 1, 7, 0, 1, 1, 1, 16, 3, 0, 83, 1, 97, 0, 100, 0, 9},
    {172, 120, 3, 51, 1, 7, 0, 1, 1, 1, 13, 2, 0, 83, 1, 94, 0, 97, 0, 9},
    {168, 120, 3, 50, 1, 0, 0, 1, 1, 1, 11, 1, 0, 83, 1, 91, 0, 94, 0, 10},
    {168, 120, 3, 50, 1, 0, 0, 1, 1, 1, 10, 0, 1, 83, 1, 88, 1, 91, 0, 10},
    {176, 120, 4, 55, 1, 1, 0, 1, 1, 1, 10, 1, 1, 84, 1, 91, 1, 88, 0, 11},
    {176, 120, 3, 55, 0, 0, 1, 1, 1, 1, 11, 2, 1, 84, 1, 94, 1, 91, 0, 11},
    {180, 120, 3, 51, 1, 1, 1, 1, 1, 1, 13, 3, 1, 84, 1, 97, 1, 94, 0, 12},
    {180, 120, 3, 51, 1, 1, 1, 1, 1, 1, 16, 4, 1, 84, 1, 100, 1, 97, 0, 12},
    {176, 120, 3, 52, 1, 2, 1, 1, 1, 1, 20, 5, 1, 84, 1, 103, 1, 100, 0, 13},
    {176, 120, 3, 52, 1, 2, 1, 1, 1, 1, 25, 6, 1, 84, 1, 106, 1, 103, 0, 13},
    {172, 120, 3, 51, 1, 3, 1, 1, 1, 1, 31, 7, 1, 84, 1, 109, 1, 106, 0, 14},
    {172, 120, 3, 51, 1, 3, 1, 1, 1, 1, 38, 8, 1, 84, 1, 112, 0, 109, 0, 14},
    {168, 120, 3, 50, 1, 4, 1, 1, 1, 1, 46, 9, 1, 84, 1, 109, 0, 112, 0, 15},
    {168, 120, 3, 50, 1, 4, 1, 1, 1, 1, 55, 10, 1, 84, 1, 106, 0, 109, 0, 15},
    {164, 120, 3, 51, 1, 5, 1, 1, 1, 1, 65, 11, 1, 84, 1, 103, 0, 106, 0, 16},
    {164, 120, 3, 51, 1, 5, 1, 1, 1, 1, 76, 12, 1, 84, 1, 100, 0, 103, 0, 16},
    {160, 120, 3, 52, 1, 6, 1, 1, 1, 1, 88, 13, 1, 84, 1, 97, 0, 100, 0, 17},
    {160, 120, 3, 52, 1, 6, 1, 1, 1, 1, 101, 14, 1, 84, 1, 94, 0, 97, 0, 17},
    {156, 120, 3, 51, 1, 7, 1, 1, 1, 1, 115, 15, 1, 84, 1, 91, 0, 94, 0, 18},
    {156, 120, 3, 51, 1, 7, 1, 1, 1, 1, 130, 16, 1, 84, 1, 88, 1, 91, 0, 18},
    {152, 120, 3, 50, 1, 0, 1, 1, 1, 1, 146, 17, 1, 84, 1, 91, 1, 88, 0, 19},
    {152, 120, 3, 50, 1, 0, 1, 1, 1, 1, 163, 18, 1, 84, 1, 94, 1, 91, 0, 19},
    {148, 120, 3, 51, 1, 1, 1, 1, 1, 1, 181, 19, 1, 84, 1, 97, 1, 94, 0, 20},
    {148, 120, 3, 51, 1, 1, 1, 1, 1, 1, 220, 20, 0, 84, 0, 100, 1, 97, 0, 20},
    {148, 120, 5, 60, 1, 1, 7999, 1, 1, 0, 200, 19, 0, 83, 0, 103, 1, 100, 0, 21},
    {148, 120, 5, 61, 1, 1, 7999, 1, 1, 0, 181, 18, 0, 83, 1, 106, 1, 103, 0, 21},
    {148, 120, 5, 61, 1, 1, 7998, 1, 1, 0, 163, 17, 0, 83, 1, 109, 1, 106, 0, 22},
    {148, 120, 5, 60, 1, 1, 7998, 1, 1, 0, 146, 16, 0, 83, 1, 112, 0, 109, 0, 22},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 130, 15, 0, 83, 1, 109, 0, 112, 1, 23},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 115, 14, 0, 83, 1, 106, 0, 109, 1, 23},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 101, 13, 0, 83, 1, 103, 0, 106, 1, 24},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 88, 12, 0, 83, 1, 100, 0, 103, 1, 24},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 76, 11, 0, 83, 1, 97, 0, 100, 1, 25},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 65, 10, 0, 83, 1, 94, 0, 97, 1, 25},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 55, 9, 0, 83, 1, 91, 0, 94, 1, 26},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 46, 8, 0, 83, 1, 88, 1, 91, 1, 26},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 38, 7, 0, 83, 1, 91, 1, 88, 1, 27},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 31, 6, 0, 83, 1, 94, 1, 91, 1, 27},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 25, 5, 0, 83, 1, 97, 1, 94, 1, 28},
    {148, 120, 6, 60, 1, 1, 7998, 1, 0, 0, 20, 4, 0, 83, 1, 100, 1, 97, 1, 28},
}};

}  // namespace

TEST_CASE("56-frame monster/fish/platform scenario matches the reference") {
    CollisionBitmap col;
    for (int x = 0; x < 320; ++x) col.set_bit(x, 150);

    Entity mon;
    mon.obj_type = ObjType::RedDino;
    mon.x = 200; mon.y = 120;
    mon.init_x = 200; mon.init_y = 120;
    mon.state = static_cast<int>(MonsterState::Reset);
    mon.respawns = 1;
    mon.spr_num = 50; mon.init_spr = 48; mon.away_spr = 55; mon.ko_spr = 60;
    mon.hits_to_ko = 2;
    mon.walk_offsets = {0, 1, 2, 1};
    mon.probe_di = 20; mon.probe_si = 30;
    mon.direction_flag = 1;
    mon.visible = false;

    Entity fish;
    fish.obj_type = ObjType::Fish;
    fish.x = 80; fish.y = 220; fish.state = 0; fish.dy = 20;
    fish.visible = false;

    Entity plat;
    plat.obj_type = ObjType::Platform;
    plat.x = 150; plat.current_y = 100;
    plat.y_top = 90; plat.y_bottom = 110;
    plat.direction = 0; plat.speed = 3;

    std::vector<Entity> ents = {mon, fish, plat};
    const int px = 100, py = 121;
    int l3a = 0;

    for (int f = 0; f < 56; ++f) {
        Entity& m = ents[0];
        if (f == 20) {  // club hit lands (collision side sets the state)
            m.state = static_cast<int>(MonsterState::RunningAway);
            m.state_counter = 0;
        }
        if (f == 30) m.ko_counter = 1;
        if (f == 44) m.state = static_cast<int>(MonsterState::Dead);

        const auto res = update_entities(ents, px, py, f, &col, l3a,
                                         /*kill_all=*/f == 40);
        l3a = res.l3a_phase_counter;

        const Row& e = kOracle[static_cast<std::size_t>(f)];
        const Entity& fi = ents[1];
        const Entity& pl = ents[2];
        INFO("frame ", f);
        CHECK(m.x == e.mx);
        CHECK(m.y == e.my);
        CHECK(m.state == e.mstate);
        CHECK(m.sprite == e.mspr);
        CHECK(m.direction == e.mdir);
        CHECK(m.state_counter == e.msc);
        CHECK(m.ko_counter == e.mko);
        CHECK(static_cast<int>(m.visible) == e.mvis);
        CHECK(static_cast<int>(m.active) == e.mact);
        CHECK(m.respawns == e.mres);
        CHECK(fi.y == e.fy);
        CHECK(fi.dy == e.fdy);
        CHECK(fi.state == e.fstate);
        CHECK(fi.sprite == e.fspr);
        CHECK(static_cast<int>(fi.visible) == e.fvis);
        CHECK(pl.current_y == e.pcy);
        CHECK(pl.direction == e.pdir);
        CHECK(pl.y == e.py);
        CHECK(static_cast<int>(res.screen_clear_of_monsters) == e.clear);
        CHECK(l3a == e.l3a);
    }
}

TEST_CASE("DEAD with respawns pending resets to RESET at init position") {
    Entity m;
    m.obj_type = ObjType::GreenDino;
    m.state = static_cast<int>(MonsterState::Dead);
    m.respawns = 2;
    m.x = 99; m.y = 77;
    m.init_x = 30; m.init_y = 120;
    std::vector<Entity> ents = {m};
    const auto res = update_entities(ents, 100, 121, 0, nullptr, 0);
    CHECK(ents[0].state == static_cast<int>(MonsterState::Reset));
    CHECK(ents[0].respawns == 1);
    CHECK(ents[0].x == 30);
    CHECK(ents[0].y == 120);
    CHECK_FALSE(ents[0].visible);
    // Respawn pending → screen NOT clear.
    CHECK_FALSE(res.screen_clear_of_monsters);
}

TEST_CASE("axe-powered: away->heading transition collapses straight to KO") {
    Entity m;
    m.obj_type = ObjType::RedDino;
    m.state = static_cast<int>(MonsterState::RunningAway);
    m.state_counter = 1;          // next transition completes the cycle
    m.hits_to_ko = 5;             // far from the normal threshold
    m.ko_spr = 60;
    std::vector<Entity> ents = {m};
    update_entities(ents, 100, 121, 1 /*odd frame*/, nullptr, 0,
                    /*kill_all=*/false, /*axe_powered=*/true);
    CHECK(ents[0].state == static_cast<int>(MonsterState::Ko));
    CHECK(ents[0].ko_counter == 40);
}

TEST_CASE("egg and rock hit-point sprites; zero HP deactivates") {
    Entity egg;
    egg.obj_type = ObjType::Egg;
    egg.counter = 2;
    std::vector<Entity> ents = {egg};
    update_entities(ents, 0, 0, 0, nullptr, 0);
    CHECK(ents[0].sprite == 125);
    ents[0].counter = 1;
    update_entities(ents, 0, 0, 1, nullptr, 0);
    CHECK(ents[0].sprite == 126);
    ents[0].counter = 0;
    update_entities(ents, 0, 0, 2, nullptr, 0);
    CHECK_FALSE(ents[0].active);

    Entity rock;
    rock.obj_type = ObjType::Rock;
    rock.counter = 3;
    std::vector<Entity> ents2 = {rock};
    update_entities(ents2, 0, 0, 0, nullptr, 0);
    CHECK(ents2[0].sprite == 143);
}
