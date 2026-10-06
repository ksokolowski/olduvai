// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/collision_dispatch.hpp"

#include <cstdlib>

#include "core/game_tables.hpp"
#include "systems/cave_logic.hpp"   // kSprCaveDescent1 (arm-frame draw)

namespace olduvai::systems {

using core::Entity;
using core::ObjType;


void dispatch_bonus_activate(SystemsState& state, int bt) {
    if (bt < 0 || bt > 5) return;   // out-of-range types are a NOP
    PlayerState& p = state.player;
    switch (bt) {
        case 0:  // spring power-up: y_vel := 0x22
            p.y_vel = 0x22;
            break;
        case 1:  // bomb: kill-all flag + palette flash
            state.bonus_trigger = 1;
            state.flash_frames = kBombFlashFrames;
            break;
        case 2:  // timer +30, clamp 99, HUD flash 60 frames
            state.timer = std::min(state.timer + 0x1E, 0x63);
            state.timer_counter = 0x3C;
            break;
        case 3:  // extra life, clamp 99
            if (p.lives < 0x63) ++p.lives;
            break;
        case 4:  // shield: 99-frame invulnerability
            p.hit_counter = 0x63;
            break;
        case 5:  // axe-powered flag
            state.halo_flight_flag = 1;
            break;
        default: break;
    }
}

void update_fireball(SystemsState& state) {
    if (state.fireball_flag == 0) return;
    if (state.fireball_flag == 1) state.fireball_x += 8;
    else state.fireball_x -= 8;
    if (state.fireball_x < -16 || state.fireball_x > core::kGameW + 16) {
        state.fireball_flag = 0;
        return;
    }
    const PlayerState& p = state.player;
    if (p.death_counter == 0 && p.hit_counter == 0) {
        if (std::abs(state.fireball_x - p.x) < 20 &&
            std::abs(state.fireball_y - p.y) < 16) {
            state.fireball_flag = 0;
            hit_player(state, 1);
        }
    }
}

void apply_sign_teleport(SystemsState& state, int screen, int x, int y) {
    // EXE seg:0da6: 0x9872=1, 0x9c68=screen, 0x97fe=x, 0x9864=y, 0x9874=0x27
    // (hit_counter 39); the transition code copies x/y into player and restart.
    PlayerState& p = state.player;
    state.cave_flag = 0;
    state.cave_index = -1;
    state.current_screen = screen;
    p.x = x;
    p.y = y;
    p.restart_x = x;
    p.restart_y = y;
    p.restart_screen = screen;
    p.restart_glider = false;   // cave-sign checkpoint is grounded
    p.restart_cave_index = -1;
    p.restart_secret_index = -1;
    p.gravity_flag = 0;
    p.hit_counter = 0x27;
    state.screen_change = true;
    state.transition_skip = true;   // collision bitmap is still the cave's
}

bool try_complete_sign_teleport(SystemsState& state) {
    if (!state.pending_sign_teleport || state.teleport_out_ticks != 0) {
        return false;
    }
    state.pending_sign_teleport = false;
    apply_sign_teleport(state, state.pending_tel_screen, state.pending_tel_x,
                        state.pending_tel_y);
    state.teleport_in_ticks = 15;   // 3 empty + 9 clouds + 3 POSE
    state.teleport_fx_x = state.pending_tel_x;
    state.teleport_fx_y = state.pending_tel_y;
    return true;
}

void apply_spring_launch(SystemsState& state,
                         const core::CollisionResult& result) {
    PlayerState& p = state.player;
    // Enhanced: a player emerging from a cave is frozen, and a spring waits
    // for the emerge to end (the L7 fake cave's exit door stands on the lava
    // spring).  Classic has no freeze and launches at once, as the EXE.
    if (state.enhanced_active && state.cave_emerge_frames > 0) return;
    // Generic trampoline bounce.
    if (result.spring_bounce && p.gravity_flag == 0) {
        p.saved_y_vel = p.y_vel;
        p.y_vel = kSpringYVel;
        p.gravity_flag = 1;
        state.sfx_spring_pending = true;
    }
    // Lava spring — type-specific launch formula.
    if (result.peak_l7_spring) {
        p.x += result.peak_l7_x_delta;
        p.y += result.peak_l7_y_delta;
        p.saved_y_vel = p.y_vel;
        p.y_vel = result.peak_l7_y_vel;
        p.gravity_flag = 1;
        state.sfx_spring_pending = true;
    }
}

namespace {

// The result walk's two deepest blocks, each over the state and the
// collision result.

void apply_cave_entrance(SystemsState& state,
                         const core::CollisionResult& result) {
    PlayerState& p = state.player;
    // Cave entrance: arm the 2-frame descent animation (sprites 44 → 45).
    if (result.cave_enter >= 0 && state.cave_flag == 0 && state.input.down) {
        if (state.cave_entrance_mask == 0 && p.cave_warp_freeze == 0) {
            state.cave_entrance_mask = (result.cave_enter << 2) | 1;
            // 2A04:0d09-0d0d (TYPE 0x12 handler): player_x = entrance_x before
            // the descent arms (the +-8 px snap lines the sprite up with the
            // hole art).
            for (const Entity& e : state.entities) {  // snap to entrance x
                if (e.obj_type == ObjType::CaveEntrance &&
                    e.counter == result.cave_enter) {
                    p.x = e.x;
                    break;
                }
            }
            // The arm frame already shows the first descent sprite:
            // Objects_Update (2A04; L1 main 21f3:0313) arms the mask and
            // FUN_27f7_1b51 (21f3:043a) draws (mask&3)+0x2c at player_x+4 in
            // the same frame, then increments (27f7:1b9c) and skips
            // walk/gravity.  Our descent tick runs before run_frame, so do the
            // same here.
            p.sprite = kSprCaveDescent1;
            p.dx = 4;                       // FUN_27f7_1b51 0x1b7f
            ++state.cave_entrance_mask;     // FUN_27f7_1b51 0x1b9c
            state.skip_player_update = true;   // 1b51 mask branch: no walk
        }
    }
}

void apply_cave_sign_teleport(SystemsState& state,
                              const core::CollisionResult& result) {
    const PlayerState& p = state.player;
    // Cave sign teleport out of the cave to a surface destination.
    if (result.cave_sign_screen >= 0 && state.cave_flag) {
        // Enhanced teleport clouds: defer the teleport while the departure
        // clouds play at the sign; the countdown in game_app applies it and
        // arms the arrival. Classic teleports at once, as the EXE.  The pending
        // gate swallows the sign's per-frame re-triggers.
        if (state.enhanced_active) {
            if (!state.pending_sign_teleport &&
                state.teleport_out_ticks == 0) {
                state.pending_sign_teleport = true;
                state.pending_tel_screen = result.cave_sign_screen;
                state.pending_tel_x = result.cave_sign_x;
                state.pending_tel_y = result.cave_sign_y;
                state.teleport_out_ticks = 12;  // 3 POSE + 3 cloud stages x 3
                state.teleport_fx_x = p.x;
                state.teleport_fx_y = p.y;
            }
        } else {
            apply_sign_teleport(state, result.cave_sign_screen,
                                result.cave_sign_x, result.cave_sign_y);
        }
    }
}

// The player's view of this frame for check_player_collisions.
CollisionContext collision_context(const SystemsState& state) {
    const PlayerState& p = state.player;
    const auto& caves = core::game_tables().cave_sizes;
    int cave_exit_x = -1;
    if (state.cave_flag && state.cave_index >= 0 &&
        state.cave_index < static_cast<int>(caves.size())) {
        cave_exit_x = caves[static_cast<std::size_t>(state.cave_index)] -
                      kCaveExitOffset;
    }
    CollisionContext ctx;
    ctx.player_x = p.x;
    ctx.player_y = p.y;
    ctx.attacking = p.club_flag > 0;
    ctx.club_flag = p.club_flag;
    ctx.facing_left = p.facing_left != 0;
    ctx.key_up = state.input.jump;
    ctx.key_down = state.input.down;
    ctx.climbing = p.climbing != 0;
    ctx.cave_exit_x = cave_exit_x;
    ctx.axe_flag = state.halo_flight_flag;   // axe-powered (DS:0x97f8)
    ctx.level = state.current_level;
    ctx.gravity_flag = p.gravity_flag;
    return ctx;
}

// A bonus touched this frame, else the first rising icon that finished its
// arc above the player (consumed); the bonus's effect and its sound.
void apply_bonus(SystemsState& state, core::CollisionResult& result) {
    if (result.bonus_type < 0) {
        for (Entity& e : state.entities) {
            if (e.bonus_pending) {
                result.bonus_type = e.mask & 0x7F;
                e.bonus_pending = false;
                e.active = false;
                e.visible = false;
                break;
            }
        }
    }
    if (result.bonus_type >= 0) {
        dispatch_bonus_activate(state, result.bonus_type);
        state.sfx_generic_pending = true;
    }
}

void apply_pickups(SystemsState& state, const core::CollisionResult& result) {
    if (result.food_collected > 0) {
        state.food_count += result.food_collected;
        state.sfx_generic_pending = true;
    }
    if (result.score_gained) add_score(state, result.score_gained);
    for (const auto& ev : result.score_events) {
        add_score_popup(state, ev.x, ev.y, ev.value);
    }
}

// Climbing enter / clamp / exit.  y_vel is never touched here (the stairs
// handler never writes it), so a spring's boosted velocity survives a ladder
// grab.
void apply_climb(PlayerState& p, const core::CollisionResult& result) {
    if (result.can_climb && !p.climbing) {
        p.climbing = 1;
        p.walk_frame = 0;
        p.gravity_flag = 0;
        p.facing_left = 0;            // ladder sprite never flips
        p.x = result.climb_x;
        p.climb_y_min = result.climb_y_bottom;
        p.climb_y_max = result.climb_y_top;
    }
    if (p.climbing) {
        p.gravity_flag = 0;
        if (p.y < p.climb_y_min) p.y = p.climb_y_min;
        if (p.y > p.climb_y_max) p.y = p.climb_y_max;
    }
    if (result.climb_exit_top && p.climbing) {
        p.climbing = 0;
        p.y = result.climb_exit_y;
    }
    if (result.climb_exit_bottom && p.climbing) p.climbing = 0;
}

// Platform riding: snap on top.
void apply_platform(PlayerState& p, const core::CollisionResult& result) {
    p.platform_flag = 0;
    if (result.platform_y >= 0 && p.gravity_flag == 0) {
        p.platform_flag = 1;
        p.y = result.platform_y - 29;
    }
}

}  // namespace

void process_entity_collisions(SystemsState& state) {
    if (state.player.death_counter > 0) return;

    auto result = check_player_collisions(state.entities,
                                          collision_context(state));
    if (result.damage) hit_player(state, result.damage);
    if (result.monster_hit) state.sfx_hit_pending = true;
    apply_bonus(state, result);
    apply_pickups(state, result);
    apply_spring_launch(state, result);
    apply_climb(state.player, result);
    apply_cave_entrance(state, result);
    apply_cave_sign_teleport(state, result);
    apply_platform(state.player, result);
}

}  // namespace olduvai::systems
