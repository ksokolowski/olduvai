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
    PlayerState& p = state.player;
    if (p.death_counter == 0 && p.hit_counter == 0) {
        if (std::abs(state.fireball_x - p.x) < 20 &&
            std::abs(state.fireball_y - p.y) < 16) {
            state.fireball_flag = 0;
            hit_player(state, 1);
        }
    }
}

void apply_sign_teleport(SystemsState& state, int screen, int x, int y) {
    // EXE seg:0da6: 0x9872=1, 0x9c68=screen, 0x97fe=x, 0x9864=y,
    // 0x9874=0x27 (hit_counter=39); transition code copies into
    // player_x/restart_x + player_y/restart_y.
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

void process_entity_collisions(SystemsState& state) {
    PlayerState& p = state.player;
    if (p.death_counter > 0) return;

    int cave_exit_x = -1;
    if (state.cave_flag && state.cave_index >= 0 &&
        state.cave_index < static_cast<int>(core::game_tables().cave_sizes.size())) {
        cave_exit_x = core::game_tables().cave_sizes[static_cast<std::size_t>(
                          state.cave_index)] - kCaveExitOffset;
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

    auto result = check_player_collisions(state.entities, ctx);

    if (result.damage) hit_player(state, result.damage);
    if (result.monster_hit) state.sfx_hit_pending = true;

    // Rising-bonus completion (the icon finished its arc above the player).
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

    if (result.food_collected > 0) {
        state.food_count += result.food_collected;
        state.sfx_generic_pending = true;
    }
    if (result.score_gained) add_score(state, result.score_gained);
    for (const auto& ev : result.score_events) {
        add_score_popup(state, ev.x, ev.y, ev.value);
    }

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

    // Climbing enter / clamp / exits.  y_vel is deliberately NOT touched
    // anywhere here (the stairs handler never writes it — preserving the
    // spring power-up's boosted velocity across ladder grabs).
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

    // Cave entrance: arm the 2-frame descent animation (sprites 44 → 45).
    if (result.cave_enter >= 0 && state.cave_flag == 0 && state.input.down) {
        if (state.cave_entrance_mask == 0 && p.cave_warp_freeze == 0) {
            state.cave_entrance_mask = (result.cave_enter << 2) | 1;
            // EXE 2A04:0d09-0d0d (TYPE 0x12 handler): player_x = entrance_x
            // before the descent arms — the +-8 px snap aligns the sprite
            // with the hole art.  Faithful; owner-questioned + byte-verified
            // 2026-07-05.
            for (const Entity& e : state.entities) {  // snap to entrance x
                if (e.obj_type == ObjType::CaveEntrance &&
                    e.counter == result.cave_enter) {
                    p.x = e.x;
                    break;
                }
            }
            // EXE: the ARM frame already shows the first descent sprite —
            // Objects_Update (2A04, L1 main 21f3:0313) arms the mask and
            // FUN_27f7_1b51 (21f3:043a) draws (mask&3)+0x2c at player_x+4
            // within the SAME frame, then increments (27f7:1b9c) and skips
            // walk/gravity.  The port's descent tick runs before run_frame,
            // so mirror the same-frame draw + increment here; without it the
            // arm tick presented one extra snapped-standing frame the EXE
            // never shows.  Finding cave_enter_exit_presentation_model.md.
            p.sprite = kSprCaveDescent1;
            p.dx = 4;                       // FUN_27f7_1b51 0x1b7f
            ++state.cave_entrance_mask;     // FUN_27f7_1b51 0x1b9c
            state.skip_player_update = true;   // 1b51 mask branch: no walk
        }
    }

    // Cave sign teleport out of the cave to a surface destination.
    if (result.cave_sign_screen >= 0 && state.cave_flag) {
        // Enhanced #20 — teleport cloud sequence: in enhanced mode the
        // teleport is DEFERRED 3 ticks so the departure clouds (87→86→85,
        // big→small, player hidden) play at the sign-cross spot; the
        // countdown completion in game_app applies the teleport and arms
        // the arrival sequence (empty→85→86→85 → player in the EXE's own
        // 0x27-tick halo shield).  Classic teleports immediately, exactly
        // as the EXE.  The pending gate swallows the per-frame sign
        // re-triggers while the clouds play.
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

    // Platform riding: snap on top.
    p.platform_flag = 0;
    if (result.platform_y >= 0 && p.gravity_flag == 0) {
        p.platform_flag = 1;
        p.y = result.platform_y - 29;
    }
}

}  // namespace olduvai::systems
