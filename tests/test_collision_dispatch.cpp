// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Dispatch-layer tests: bonus activation, score accumulation, popups,
// fireball flight, and the collision-result application paths.

#include "doctest/doctest.h"
#include "core/game_tables.hpp"
#include "systems/collision_dispatch.hpp"
#include "systems/transitions.hpp"

using namespace olduvai;
using namespace olduvai::core;
using namespace olduvai::systems;

TEST_CASE("bonus dispatch applies the six effects; out-of-range is a NOP") {
    SystemsState s;
    dispatch_bonus_activate(s, 0);
    CHECK(s.player.y_vel == 0x22);             // spring
    dispatch_bonus_activate(s, 1);
    CHECK(s.bonus_trigger == 1);               // bomb
    CHECK(s.flash_frames == 20);
    s.timer = 80;
    dispatch_bonus_activate(s, 2);
    CHECK(s.timer == 99);                      // +30 clamped to 99
    CHECK(s.timer_counter == 0x3C);
    const int lives = s.player.lives;
    dispatch_bonus_activate(s, 3);
    CHECK(s.player.lives == lives + 1);
    dispatch_bonus_activate(s, 4);
    CHECK(s.player.hit_counter == 0x63);       // shield
    dispatch_bonus_activate(s, 5);
    CHECK(s.halo_flight_flag == 1);            // axe-powered
    SystemsState t;
    dispatch_bonus_activate(t, 6);             // NOP
    dispatch_bonus_activate(t, -1);
    CHECK(t.player.y_vel == kJumpYVel);
    CHECK(t.bonus_trigger == 0);
}

TEST_CASE("add_score: 999,999 cap and extra life per 10K") {
    SystemsState s;
    s.player.lives = 3;
    add_score(s, 9999);
    CHECK(s.player.lives == 3);
    add_score(s, 1);                            // crosses 10,000
    CHECK(s.player.lives == 4);
    CHECK(s.next_life_score == 20000);
    add_score(s, 25000);                        // crosses 20K and 30K
    CHECK(s.player.lives == 6);
    add_score(s, 2000000);
    CHECK(s.score == 999999);                   // cap
    CHECK(s.player.lives > 6);                  // lives kept up with hiscore
    CHECK(s.sfx_generic_pending);
}

TEST_CASE("score popups: slot allocation, draw-before-move ordering") {
    SystemsState s;
    add_score_popup(s, 50, 60, 100);            // sprite 33 + 2
    add_score_popup(s, 70, 80, 7);              // no sprite for 7 → ignored
    CHECK(s.score_bonuses[0].counter == 10);
    CHECK(s.score_bonuses[0].sprite == 35);
    CHECK(s.score_bonuses[1].counter == 0);
    update_score_bonuses(s);                    // pre-render decrement
    CHECK(s.score_bonuses[0].counter == 9);
    CHECK(s.score_bonuses[0].y == 60);          // not moved yet (draw first)
    move_score_bonuses(s);                      // post-render move
    CHECK(s.score_bonuses[0].y == 56);
}

TEST_CASE("score popups: cleared on screen change (EXE L1-main 0x289)") {
    // A popup spawned in a cave must not keep floating on the surface after exit
    // — the per-screen setup zeroes the popup array (DS:0x9806) every screen
    // change.  clear_per_screen_state mirrors that clear loop.
    SystemsState s{};
    add_score_popup(s, 50, 60, 100);
    add_score_popup(s, 80, 90, 50);
    CHECK(s.score_bonuses[0].counter == 10);
    CHECK(s.score_bonuses[1].counter == 10);
    clear_per_screen_state(s);
    for (const auto& b : s.score_bonuses) {
        CHECK(b.counter == 0);
        CHECK(b.active_this_frame == false);
    }
}

TEST_CASE("fireball: flight, despawn, hit") {
    SystemsState s;
    s.fireball_flag = 1;
    s.fireball_x = 100;
    s.fireball_y = 120;
    s.player.x = 300;
    s.player.y = 0;
    update_fireball(s);
    CHECK(s.fireball_x == 108);                 // +8/frame rightward
    s.fireball_x = core::kGameW + 10;
    update_fireball(s);
    CHECK(s.fireball_flag == 0);                // off-screen despawn
    // Direct hit.
    SystemsState h;
    h.fireball_flag = 2;
    h.fireball_x = 110;
    h.fireball_y = 120;
    h.player.x = 95;
    h.player.y = 121;
    h.player.hit_counter = 0;
    update_fireball(h);                         // moves to 102, |102-95|<20
    CHECK(h.fireball_flag == 0);
    CHECK(h.player.energy == core::kInitialEnergy - 1);
    CHECK(h.player.hit_counter == 0x13);
}

TEST_CASE("dispatch: ghost rise completion activates the bonus") {
    SystemsState s;
    s.player.hit_counter = 0;
    Entity ghost;
    ghost.obj_type = ObjType::AncestorGhost;
    ghost.bonus_pending = true;
    ghost.mask = 5;                             // axe
    s.entities.push_back(ghost);
    process_entity_collisions(s);
    CHECK(s.halo_flight_flag == 1);
    CHECK_FALSE(s.entities[0].active);
    CHECK(s.sfx_generic_pending);
}

TEST_CASE("dispatch: lava spring applies the type-specific launch") {
    SystemsState s;
    s.player.x = 175;
    s.player.y = 121;
    s.player.hit_counter = 0;
    Entity peak;
    peak.obj_type = ObjType::PeakL7;
    peak.x = 170;
    peak.y = 133;                               // py == y - 12
    s.entities.push_back(peak);
    process_entity_collisions(s);
    CHECK(s.player.x == 180);                   // +5
    CHECK(s.player.y == 111);                   // -10
    CHECK(s.player.y_vel == 0xA8);
    CHECK(s.player.gravity_flag == 1);
    CHECK(s.sfx_spring_pending);
}

TEST_CASE("dispatch: climb enter snaps x and preserves y_vel") {
    SystemsState s;
    s.player.x = 52;
    s.player.y = 140;
    s.player.y_vel = 0x22;                      // spring power-up active
    s.input.jump = true;                        // key_up
    Entity stairs;
    stairs.obj_type = ObjType::Stairs;
    stairs.x = 50;
    stairs.y_top = 140;
    stairs.y_bottom = 80;
    stairs.visible = false;
    s.entities.push_back(stairs);
    process_entity_collisions(s);
    CHECK(s.player.climbing == 1);
    CHECK(s.player.x == 54);                    // stairs_x + 4
    CHECK(s.player.y_vel == 0x22);              // NOT reset
    CHECK(s.player.climb_y_min == 80);
    CHECK(s.player.climb_y_max == 140);
}

TEST_CASE("dispatch: cave sign teleports out and clears restart mode") {
    // Cave widths are runtime game data; give the scenario its own width.
    core::GameTables gt;
    gt.cave_sizes[0] = 96;
    core::install_game_tables(gt);
    SystemsState s;
    s.cave_flag = 1;
    s.cave_index = 0;                           // width 96 → exit_x = 88
    s.player.x = 85;                            // past exit_x - 8 (= 80)
    s.player.y = 121;
    s.player.restart_cave_index = 0;
    Entity sign;
    sign.obj_type = ObjType::CaveSign;
    sign.counter = 7;
    sign.init_x = 44;
    sign.init_y = 99;
    sign.visible = false;
    s.entities.push_back(sign);
    process_entity_collisions(s);
    CHECK(s.cave_flag == 0);
    CHECK(s.current_screen == 7);
    CHECK(s.player.x == 44);
    CHECK(s.player.restart_cave_index == -1);
    CHECK(s.player.hit_counter == 0x27);
    CHECK(s.screen_change);
    CHECK(s.transition_skip);
    core::install_game_tables(core::GameTables{});   // reset for other tests
}

TEST_CASE("deferred sign teleport completes at the logic step, not before") {
    // Enhanced #20 regression (2026-07-06): completing the deferred teleport
    // in the END-OF-TICK presentation block mutated cave_flag/current_screen
    // AFTER the transition classifier's snapshot bracket, so the next tick
    // classified the cave->surface change as a surface pan-scroll instead of
    // the cave fade pair.  The completion is now a logic-step helper the
    // game loop calls AFTER its pre-frame snapshot and BEFORE run_frame.
    SystemsState s;
    s.enhanced_active = true;
    s.cave_flag = 1;
    s.cave_index = 2;
    s.current_screen = 102;
    s.pending_sign_teleport = true;
    s.pending_tel_screen = 7;
    s.pending_tel_x = 120;
    s.pending_tel_y = 80;

    SUBCASE("no-op while the departure clouds still play") {
        s.teleport_out_ticks = 3;
        CHECK(!try_complete_sign_teleport(s));
        CHECK(s.pending_sign_teleport);
        CHECK(s.current_screen == 102);
        CHECK(s.cave_flag == 1);
        CHECK(!s.screen_change);
    }

    SUBCASE("applies once the countdown is spent, arming the arrival") {
        s.teleport_out_ticks = 0;
        CHECK(try_complete_sign_teleport(s));
        CHECK(!s.pending_sign_teleport);
        CHECK(s.current_screen == 7);
        CHECK(s.cave_flag == 0);
        CHECK(s.cave_index == -1);
        CHECK(s.player.x == 120);
        CHECK(s.player.y == 80);
        CHECK(s.screen_change);
        CHECK(s.teleport_in_ticks == 15);   // 3 empty + 9 clouds + 3 POSE
        CHECK(s.teleport_fx_x == 120);
        CHECK(s.teleport_fx_y == 80);
        // second call is a no-op
        CHECK(!try_complete_sign_teleport(s));
    }

    SUBCASE("no-op without a pending teleport") {
        s.pending_sign_teleport = false;
        s.teleport_out_ticks = 0;
        CHECK(!try_complete_sign_teleport(s));
        CHECK(s.current_screen == 102);
    }
}
