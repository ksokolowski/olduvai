// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/frame_runner.hpp"

#include "systems/collision_dispatch.hpp"
#include "systems/monster_ai.hpp"
#include "systems/cave_logic.hpp"
#include "systems/secret.hpp"

namespace olduvai::systems {

bool update_falling_stone(SystemsState& state) {
    // FUN_27f7_089f: inactive → retf; move ±8 with edge deactivation at
    // [0, 320]; hitbox px+25 > sx > px-10, py+16 > sy > py-13; cave-warp
    // freeze suppresses; hit deactivates + Game_HitPlayer(1).
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

void run_frame(SystemsState& state, const FrameInputs& inputs) {
    // 1. Inputs.
    state.input.left = inputs.left;
    state.input.right = inputs.right;
    state.input.jump = inputs.jump || inputs.up;
    state.input.down = inputs.down;
    state.input.attack = inputs.attack;

    // 2. Score popup decrement (pre-render).
    update_score_bonuses(state);

    // 3. Entity update.
    const auto res = update_entities(
        state.entities, state.player.x, state.player.y, state.frame_counter,
        &state.collision, state.l3a_phase_counter,
        /*kill_all=*/state.bonus_trigger != 0,
        /*axe_powered=*/state.halo_flight_flag != 0,
        /*fireball_active=*/state.fireball_flag != 0);
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

    // 6. Player physics + animation (skipped while a descent or teleport
    // owns the player this frame).  The post-hit/spawn invulnerability
    // ticks unconditionally before the player branch, where the
    // reference loop keeps it — the expiry frame shifts otherwise.
    tick_post_hit_invuln(state.player);
    // Enhanced #20 — the teleport cloud phases own the player (hidden +
    // frozen; the ghost-paced anim must not be walked out of invisibly).
    if (state.teleport_out_ticks > 0 || state.teleport_in_ticks > 0)
        state.skip_player_update = true;
    // Enhanced #18 v2 — the cave-EMERGE ghost-paced reveal freezes the
    // player too (owner-approved "stop game time"; same idiom as the
    // teleport gate above).  ENHANCED-ONLY: the classic 2-tick emerge
    // stays draw-only so classic gameplay timing is EXE-identical.
    // Rule: a hit or death during the frozen ticks CANCELS the emerge
    // cleanly so the freeze can never wedge the player — hit_player
    // clears the counter on any real hit (lethal or not); any other
    // death path is caught by the death_counter check here.
    if (state.cave_emerge_frames > 0 && state.enhanced_active) {
        if (state.player.death_counter > 0)
            state.cave_emerge_frames = 0;
        else
            state.skip_player_update = true;
    }
    if (state.skip_player_update || state.transition_skip) {
        state.skip_player_update = false;
        state.transition_skip = false;
        // The skipped frame still services the attack latch — a press
        // on a transition frame starts the swing, exactly as the
        // original's branch does.
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
        // Flight physics shares the player slot (after collisions, like
        // the original's per-frame order); a no-op unless riding on the
        // flight screens.
        update_flight_physics(state);
        update_player(state);
    }

    // 7. Score popup post-render move.
    move_score_bonuses(state);

    // 8. Frame counter.
    ++state.frame_counter;
}

}  // namespace olduvai::systems
