// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/frame_runner.hpp"

#include <cstdlib>   // getenv — OLDUVAI_FORCE_L3_DESCENT gate hook

#include "systems/collision_dispatch.hpp"
#include "systems/monster_ai.hpp"
#include "systems/cave_logic.hpp"
#include "systems/secret.hpp"
#include "systems/transitions.hpp"

namespace olduvai::systems {

bool update_falling_stone(SystemsState& state) {
    // FUN_27f7_089f: inactive -> return; move +-8, deactivate at the [0, 320]
    // edges; hitbox px+25 > sx > px-10, py+16 > sy > py-13; suppressed during
    // cave-warp freeze; a hit deactivates it and calls Game_HitPlayer(1).
    if (state.stone_state == 0) return false;
    if (state.stone_state == 1) {
        state.stone_x += 8;
        if (state.stone_x > 320) state.stone_state = 0;
    } else {
        state.stone_x -= 8;
        if (state.stone_x < 0) state.stone_state = 0;
    }
    if (state.stone_state == 0) return false;

    const PlayerState& p = state.player;
    if (p.x + 25 <= state.stone_x) return false;
    if (p.x - 10 >= state.stone_x) return false;
    if (p.y - 13 >= state.stone_y) return false;
    if (p.y + 16 <= state.stone_y) return false;
    if (p.cave_warp_freeze != 0) return false;

    state.stone_state = 0;   // deactivate on hit
    hit_player(state, 1);
    return true;
}

void apply_inputs(SystemsState& state, const FrameInputs& inputs) {
    state.input.left = inputs.left;
    state.input.right = inputs.right;
    state.input.jump = inputs.jump || inputs.up;
    state.input.down = inputs.down;
    state.input.attack = inputs.attack;
}

void run_frame(SystemsState& state, const FrameInputs& inputs) {
    // 1. Inputs.
    apply_inputs(state, inputs);

    // 2. Score popup decrement (pre-render).
    update_score_bonuses(state);

    // 3. Entity update.
    EntityTick tick;
    tick.player_x = state.player.x;
    tick.player_y = state.player.y;
    tick.frame = state.frame_counter;
    tick.collision = &state.collision;
    tick.l3a_phase_counter = state.l3a_phase_counter;
    tick.kill_all = state.bonus_trigger != 0;
    tick.axe_powered = state.halo_flight_flag != 0;
    tick.fireball_active = state.fireball_flag != 0;
    const auto res = update_entities(state.entities, tick);
    state.l3a_phase_counter = res.l3a_phase_counter;
    state.screen_clear_of_monsters = res.screen_clear_of_monsters;

    // 3b. Balloon/glider scenery visibility — after the entity update,
    // before collisions (the original's draw-skip check order).
    sync_balloon_visibility(state);

    // 4a. Fireball spawn requests (one in flight).
    for (const core::Entity& e : state.entities) {
        if (e.fireball_request != 0 && state.fireball_flag == 0) {
            state.fireball_flag = e.fireball_request;
            state.fireball_x = e.x;
            state.fireball_y = e.y + 4;
            break;
        }
    }
    // 4b. Fireball motion + player hit.
    update_fireball(state);
    // 4c. Falling stone — surface platform levels only.
    if (!state.cave_flag && !state.secret_flag &&
        (state.current_level == 1 || state.current_level == 3 ||
         state.current_level == 5 || state.current_level == 7)) {
        update_falling_stone(state);
    }

    // 5. Player-entity collisions.
    process_entity_collisions(state);

    // 5b. The L1 ride nudge — after collisions, before the player slot.
    // Screen 9: the balloon drifts right and up.  Screen 12: catapult
    // landing until x reaches 60, then detach with a hit cooldown.
    if (state.current_level == 1 && state.glider_active) {
        if (state.current_screen == 9) {
            state.player.x += 3;
            state.player.y -= 3;
        } else if (state.current_screen == 12) {
            if (state.player.x < 60) {
                state.player.y = 80;
                state.player.x += 5;
            } else {
                state.glider_active = false;
                state.player.hit_counter = 0x27;
            }
        }
    }

    // 6. Player physics + animation, skipped while a descent or teleport owns
    // the player.  The invulnerability tick runs first, unconditionally, where
    // the reference has it (the expiry frame shifts otherwise).
    tick_post_hit_invuln(state.player);
    // Enhanced #20 — the teleport cloud phases own the player (hidden +
    // frozen; the ghost-paced anim must not be walked out of invisibly).
    if (state.teleport_out_ticks > 0 || state.teleport_in_ticks > 0)
        state.skip_player_update = true;
    // Enhanced cave emerge also freezes the player (classic's 2-tick emerge
    // stays draw-only).  A hit or death cancels it so the freeze cannot wedge:
    // hit_player clears the counter on any hit; other deaths are caught here.
    if (state.cave_emerge_frames > 0 && state.enhanced_active) {
        if (state.player.death_counter > 0)
            state.cave_emerge_frames = 0;
        else
            state.skip_player_update = true;
    }
    if (state.skip_player_update || state.transition_skip) {
        state.skip_player_update = false;
        state.transition_skip = false;
        // The skipped frame still services the attack latch (a press on a
        // transition frame starts the swing, as in the original).
        if (!state.input.attack) {
            state.player.attack_latch = 0;
        } else if (state.player.attack_latch == 0 &&
                   state.player.club_flag == 0) {
            state.player.attack_latch = 1;
            state.player.club_flag = 2;
        }
    } else if (tick_cave_descent(state)) {
        // The cave-entrance descent owns the player this frame (the
        // same slot in the reference's branch chain).
    } else {
        // Flight physics shares the player slot (after collisions, as in the
        // original); a no-op unless riding.
        update_flight_physics(state);
        update_player(state);
    }

    // 7. Score popup post-render move.
    move_score_bonuses(state);

    // 8. Frame counter.
    ++state.frame_counter;
}

void wrap_frame_counter(SystemsState& state, bool god) {
    if (state.frame_counter <= 0x3D) return;
    state.frame_counter = 0;
    if (state.timer > 0)
        --state.timer;
    else if (state.player.death_counter == 0 && god)
        state.timer = 99;
    else if (state.player.death_counter == 0)
        trigger_death(state);
}

void set_bird_bounds(SystemsState& state, int margin) {
    for (auto& e : state.entities)
        if (e.obj_type == core::ObjType::Bird) {
            e.off_screen_left = -(margin + 50);
            e.bird_spawn_x = 355 + margin;
        }
}

void run_tick(SystemsState& state, const FrameInputs& inputs, bool paused) {
    apply_inputs(state, inputs);
    if (!paused) try_complete_sign_teleport(state);
    state.skip_player_update = tick_cave_descent(state);
    if (!paused) run_frame(state, inputs);
}

void end_tick(SystemsState& state, bool god) {
    if (god) god_refill(state);
    run_post_frame_steps(state);
}

void tick_teleport_fx(SystemsState& state) {
    if (state.teleport_out_ticks > 0)
        --state.teleport_out_ticks;
    else if (state.teleport_in_ticks > 0)
        --state.teleport_in_ticks;
}

void tick_get_ready(SystemsState& state) {
    if (state.get_ready_counter >= 2 && state.get_ready_counter <= 17 &&
        (state.frame_counter & 1) == 0)
        --state.get_ready_counter;
}

void tick_cave_emerge(SystemsState& state) {
    if (state.cave_emerge_frames > 0) --state.cave_emerge_frames;
}

void god_refill(SystemsState& state) {
    state.player.energy = 999;
    state.player.lives = 99;
    if (state.food_count < kFoodGate) state.food_count = kFoodGate;
    state.game_over = false;
}

void run_post_frame_steps(SystemsState& state) {
    // 6b. Death halo.
    if (state.player.death_counter == 1) {
        init_death_halo(state);
    } else if (state.player.death_counter > 1) {
        tick_death_halo(state);
    } else {
        state.death_halo_active = false;
    }

    // 6c/6d. L5 glider entry + the screen-12 detach/fly-away.
    check_l5_glider_entry(state);
    handle_l5_screen12_glider(state);

    // 6e. Clamp + death-by-fall before exits and transitions (they fire in
    // glider mode too, where the player update returns early).
    if (!state.screen_change) {
        clamp_player_position(state);
    }
    check_death_by_fall(state);

    // 7. Cave/secret exits (the trampoline fires before the exit
    // check so a bounce can reach the exit threshold same-frame).
    if (state.cave_flag) {
        state.secret_spring_bouncing = false;
        check_cave_exit(state);
    } else if (state.secret_flag) {
        state.secret_spring_bouncing =
            update_secret_trampoline(state);
        check_secret_exit(state);
    }

    // 8. Surface transitions; secret entry has priority.
    // OLDUVAI_FORCE_L3_DESCENT (debug): the 17->18 trunk descent needs full
    // food and the big bird KO'd, so no headless run reaches it.  Seed the
    // three inputs check_l3_transition reads (food, player.y, the bird-cleared
    // latch) after run_frame and before check_screen_transition, so the
    // unmodified transition fires (the descent byte-diff recipe).  Cheap state
    // guards precede getenv.
    if (state.current_level == 3 && state.current_screen == 17 &&
        !state.screen_change && std::getenv("OLDUVAI_FORCE_L3_DESCENT")) {
        state.food_count = kFoodGate;
        state.player.y = 0x44;
        state.screen_clear_of_monsters = true;
    }
    if (!state.cave_flag && !state.secret_flag) {
        state.secret_spring_bouncing = false;
        if (!check_secret_entry(state)) {
            check_screen_transition(state);
        }
    }

    // 8a. Cave-warp animation (not while inside a cave).
    if (!state.cave_flag) {
        check_cave_warp_animation(state);
    }
}

}  // namespace olduvai::systems
