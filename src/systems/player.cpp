// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/player.hpp"

#include "systems/cave_logic.hpp"   // kSprCaveDescent1

namespace olduvai::systems {

void PlayerState::reset_for_level(int start_x, int start_y) {
    x = start_x;
    y = start_y;
    y_vel = core::kJumpYVel;
    gravity_flag = 0;
    facing_left = 0;
    walk_frame = 0;
    dx = dy = 0;
    club_flag = 0;
    attack_latch = 0;
    axe_flag = 0;
    climbing = 0;
    cave_warp_freeze = 0;
    halo_spr = 0;
    hit_counter = 0x28;  // 40-frame spawn invulnerability
    hit_blink = 0;
    energy = core::kInitialEnergy;
    platform_flag = 0;
    death_counter = 0;
    ghost_rise = 0;
    death_save_x = death_save_y = 0;
    saved_y_vel = 0;
    sprite = kSprPlayerStand;
    restart_x = start_x;
    restart_y = start_y;
    restart_screen = 0;
    restart_cave_index = -1;
    restart_secret_index = -1;
    restart_glider = false;
}

void tick_post_hit_invuln(PlayerState& p) {
    // FUN_27f7_12c7 +0x12d6..0x12fc: 0 < hit_counter < 100 → decrement +
    // blink toggle.  Runs every frame from the main loop, on every branch.
    if (p.hit_counter > 0 && p.hit_counter < 100) {
        --p.hit_counter;
        p.hit_blink = 1 - p.hit_blink;
    }
}

void update_climbing(SystemsState& state) {
    PlayerState& p = state.player;
    p.gravity_flag = 0;
    if (state.input.jump) {
        p.y -= 4;
        p.walk_frame ^= 1;
    }
    if (state.input.down) {
        p.y += 4;
        p.walk_frame ^= 1;
    }
    p.sprite = kSprPlayerClimb1 + (p.walk_frame & 1);
}

void update_death(SystemsState& state) {
    PlayerState& p = state.player;

    if (p.death_counter == 1) {  // save position for the ghost float
        p.death_save_x = p.x;
        p.death_save_y = p.y;
    }

    if (p.death_counter < 9) {   // animation frames 1..8
        p.sprite = kDeathSpr[static_cast<std::size_t>(p.death_counter - 1)];
        p.dx = -5;
        ++p.death_counter;
        return;
    }

    // Ghost float: rise 4 px/frame from the saved position; sprite
    // alternates on the rise counter.  Attack skips to instant respawn.
    if (p.ghost_rise != 0) {
        const int rise_val = p.ghost_rise;
        ++p.ghost_rise;
        p.death_save_y -= 4;
        p.sprite = kSprPlayerGhost1 + (rise_val & 1);
        p.dx = p.death_save_x - p.x;
        p.dy = p.death_save_y - p.y;
        if (state.input.attack) p.death_save_y = -1;
        if (p.death_save_y > 0) return;  // still floating
    }
    respawn(state);
}

namespace {

// ---- update_player's phases, in order every frame ----
// `falling` is shared: phase 1 sets it, phase 4 can set it (the jump arc
// ends), phases 3 and 5 read it.

void probe_ground_and_fall(SystemsState& state, bool& falling) {
    PlayerState& p = state.player;
    const core::CollisionBitmap& col = state.collision;
// ── ground detection (two-probe) ──
if (p.gravity_flag == 0 &&
    col.test(p.x + kProbeLeftX, p.y + kProbeFootY) &&
    col.test(p.x + kProbeRightX, p.y + kProbeFootY) &&
    p.platform_flag == 0) {
    falling = true;
    p.sprite = kSprPlayerJump;
    for (int steps = 0; steps < kMaxFallSearch; ++steps) {
        ++p.y;
        if (!col.test(p.x + kProbeLeftX, p.y + kProbeFootY)) break;
        if (!col.test(p.x + kProbeRightX, p.y + kProbeFootY)) break;
    }
}
}

void save_restart_point(SystemsState& state, bool falling) {
    PlayerState& p = state.player;
// ── grounded restart-point save ──
if (p.gravity_flag == 0 && !falling && p.death_counter == 0 &&
    p.platform_flag == 0) {
    p.restart_x = p.x;
    p.restart_y = p.y;
    p.restart_screen = state.current_screen;
    p.restart_cave_index = state.cave_flag ? state.cave_index : -1;
    p.restart_secret_index = state.secret_flag ? state.secret_index : -1;
    p.restart_glider = false;   // grounded → normal grounded respawn
}
}

void tick_attack_latch(SystemsState& state) {
    PlayerState& p = state.player;
    const InputState& inp = state.input;
// ── attack initiation (edge-triggered latch) ──  // FUN_27f7_207e
if (!inp.attack) {
    p.attack_latch = 0;
} else if (p.attack_latch == 0 && p.club_flag == 0 &&
           !state.glider_active) {
    p.attack_latch = 1;
    p.club_flag = 2;
}
}

void walk_horizontal(SystemsState& state, bool falling) {
    PlayerState& p = state.player;
    const InputState& inp = state.input;
// ── walking ──
if (inp.left && p.club_flag == 0) {
    int speed = 5;
    if (!falling && p.gravity_flag == 0) {
        p.walk_frame = (p.walk_frame + 1) % 6;
        p.sprite = kWalkSpr[static_cast<std::size_t>(p.walk_frame)];
        speed = kWalkVel[static_cast<std::size_t>(p.walk_frame)];
        if (p.facing_left == 1)
            p.dx = -kWalkDx[static_cast<std::size_t>(p.walk_frame)];
    }
    if (p.facing_left != 1 && !falling && p.gravity_flag == 0)
        p.sprite = kSprPlayerTurn;
    p.facing_left = 1;
    p.x -= speed;
    if (p.gravity_flag != 0) p.x -= 2;
}
if (inp.right && p.club_flag == 0) {
    int speed = 5;
    if (!falling && p.gravity_flag == 0) {
        p.walk_frame = (p.walk_frame + 1) % 6;
        p.sprite = kWalkSpr[static_cast<std::size_t>(p.walk_frame)];
        speed = kWalkVel[static_cast<std::size_t>(p.walk_frame)];
        if (p.facing_left == 0)
            p.dx = kWalkDx[static_cast<std::size_t>(p.walk_frame)];
    }
    if (p.facing_left != 0 && !falling && p.gravity_flag == 0)
        p.sprite = kSprPlayerTurn;
    p.facing_left = 0;
    p.x += speed;
    if (p.gravity_flag != 0) p.x += 2;
}
}

void tick_gravity_arc(SystemsState& state, bool& falling) {
    PlayerState& p = state.player;
// ── gravity / jump arc ──
if (p.gravity_flag != 0) {
    int substeps = 0;
    while (p.gravity_flag != 0 && substeps < kMaxGravitySubsteps) {
        ++p.gravity_flag;
        ++substeps;
        const int vel = p.y_vel & 0x7F;

        // Jump-apex chirp trigger.  // FUN_27f7_1b51 +0x1e57..0x1e69
        if (p.gravity_flag == 2 && vel > 0x14 && (p.y_vel & 0x80) == 0)
            state.jump_apex_sfx_pending = true;

        // Two independent checks (both can fire: y -= 3).  The vel guards
        // reproduce the original's unsigned wrap (vel < 8 made the comparison
        // always false).
        if (vel >= 8 && (vel - 8) < p.gravity_flag && p.gravity_flag < vel)
            p.y -= 1;
        if (vel >= 6 && p.gravity_flag < (vel - 6))
            p.y -= 2;

        if (vel < p.gravity_flag) {
            p.gravity_flag = 0;
            falling = true;
            if (p.y_vel & 0x80) p.y_vel = p.saved_y_vel;
        }
    }
    p.sprite = kSprPlayerJump;
}
}

void tick_club_frames(SystemsState& state, bool falling) {
    PlayerState& p = state.player;
    const InputState& inp = state.input;
// ── club attack frames ──
if (p.club_flag != 0) {
    std::size_t idx = static_cast<std::size_t>(2 - p.club_flag);
    if (p.gravity_flag != 0 || falling) idx += 2;
    p.sprite = kHittingSpr[idx].spr;
    p.dy = kHittingSpr[idx].dy;
    if (inp.left) {
        p.facing_left = 1;
        p.x -= 5;
    }
    if (inp.right) {
        p.facing_left = 0;
        p.x += 5;
    }
}
}

}  // namespace

void update_player(SystemsState& state) {
    PlayerState& p = state.player;
    const InputState& inp = state.input;

    // cave_warp_freeze tick.  On L3/L7 the cave-warp animation owns the counter
    // (a per-frame >>2, freeze & 3 != 0); skip the -1 so its bits survive.
    const bool warp_active = (state.current_level == 3 ||
                              state.current_level == 7) &&
                             (p.cave_warp_freeze & 3) != 0;
    if (p.cave_warp_freeze > 0 && !warp_active) {
        --p.cave_warp_freeze;
        p.halo_spr = 1 - p.halo_spr;
    }
    // The warp's descent (FUN_27f7_1b51 +0x1b63..0x1b9c): while the low bits
    // are set, draw pose 0x2c + bits at x + 4, step the bits up to 3 and skip
    // the rest of the update.  The level main warps only once they read 3
    // (L7 +0x07e5..0x07f3, L3 +0x0e02..0x0e10; check_cave_warp_animation).
    // As for caves (tick_cave_descent), the third pose is restored for one
    // tick: the EXE's order leaves it unpresented.
    if (warp_active) {
        const int bits = p.cave_warp_freeze & 3;
        p.sprite = kSprCaveDescent1 + (bits - 1);
        p.dx = 4;   // FUN_27f7_1b51 +0x1b7f: add ax, 4
        p.dy = 0;
        if (bits != 3)
            ++p.cave_warp_freeze;   // +0x1b9c
        else
            state.cave_descent_third_shown = true;
        return;
    }

    p.dx = 0;
    p.dy = 0;
    p.sprite = kSprPlayerStand;

    if (p.death_counter > 0) {
        update_death(state);
        return;
    }
    if (p.climbing) {
        update_climbing(state);
        return;
    }
    // Balloon/glider ride: draw-only, no walk/gravity/jump.
    if (state.glider_active) return;

    bool falling = false;

    probe_ground_and_fall(state, falling);
    save_restart_point(state, falling);
    tick_attack_latch(state);
    walk_horizontal(state, falling);
    tick_gravity_arc(state, falling);

    // ── jump initiation ──
    if (!falling && p.gravity_flag == 0 && inp.jump) p.gravity_flag = 1;

    tick_club_frames(state, falling);
    // club_flag decrements in the weapon overlay draw, after rendering.
    // // FUN_27f7_1f72
}

void clamp_player_position(SystemsState& state) {
    PlayerState& p = state.player;
    if (p.death_counter != 0) return;  // skip during death
    if (p.x < 0) p.x = 0;
    if (p.x > 300) p.x = 300;
}

void check_death_by_fall(SystemsState& state) {
    if (state.player.y > 0xB4) {  // 180, all levels
        // --god in flight: the EXE's balloon/glider death respawn drops the
        // player into an unescapable fall, which infinite lives turn into a
        // loop.  Keep them at the waterline instead (god only; grounded god
        // falls respawn normally).
        if (state.god_mode && state.glider_active) {
            state.player.y = 0xB4;
            return;
        }
        trigger_death(state);
    }
}

void trigger_death(SystemsState& state) {
    PlayerState& p = state.player;
    if (p.death_counter == 0) {
        p.hit_counter = 0;
        p.ghost_rise = 1;
        p.death_counter = 1;
        p.energy = 0;
        // Capture the death halo now, while glider_active is still set:
        // update_death moves death_counter past 1 before the post-frame == 1
        // gate could see it.
        init_death_halo(state);
    }
}

void hit_player(SystemsState& state, int damage) {
    PlayerState& p = state.player;
    if (p.hit_counter != 0 || p.death_counter != 0) return;  // entry guard
    // Enhanced: a real new hit during the frozen cave-emerge cancels it.  After
    // the EXE entry guard, so stale invulnerability from before the exit does
    // not.
    if (state.enhanced_active) state.cave_emerge_frames = 0;
    if (p.energy > damage) {
        p.energy -= damage;
        p.hit_counter = 0x13;  // 19-frame cooldown
        if (state.god_mode) p.energy = 999;
        return;
    }
    // Lethal path — suppressed while a cave warp is active.
    if (p.cave_warp_freeze != 0) return;
    if (state.god_mode) {
        p.energy = 999;
        p.hit_counter = 0x13;
        return;
    }
    p.energy = 0;
    p.ghost_rise = 1;
    p.death_counter = 1;
    init_death_halo(state);   // capture death-display while glider_active set
}

void respawn(SystemsState& state) {
    PlayerState& p = state.player;
    --p.lives;
    if (p.lives < 0) {
        state.game_over = true;
        return;
    }

    p.x = p.restart_x;
    p.y = p.restart_y;
    if (p.x < 6) p.x = 6;       // respawn x clamp [6, 285]
    if (p.x > 285) p.x = 285;

    // Restore cave/secret mode atomically with current_screen (the engine
    // invariant layered over the original's current_screen >= 100 mode).
    const bool screen_changed = (p.restart_screen != state.current_screen);
    const bool prev_cave = state.cave_flag != 0;
    const bool prev_secret = state.secret_flag != 0;
    const bool cave_state_changed =
        prev_cave != (p.restart_cave_index >= 0) ||
        prev_secret != (p.restart_secret_index >= 0);
    if (p.restart_cave_index >= 0) {
        state.cave_flag = 1;
        state.cave_index = p.restart_cave_index;
        state.secret_flag = 0;
        state.secret_index = -1;
    } else if (p.restart_secret_index >= 0) {
        state.secret_flag = 1;
        state.secret_index = p.restart_secret_index;
        state.cave_flag = 0;
        state.cave_index = -1;
    } else {
        state.cave_flag = 0;
        state.cave_index = -1;
        state.secret_flag = 0;
        state.secret_index = -1;
    }
    if (screen_changed) {
        state.current_screen = p.restart_screen;
        state.screen_change = true;
    } else if (cave_state_changed) {
        // Same screen but the mode flipped — collision must rebuild.
        state.screen_change = true;
    }

    p.ghost_rise = 0;
    p.death_counter = 0;
    p.climbing = 0;            // death-while-climbing must not persist
    p.facing_left = 0;
    p.walk_frame = 0;
    p.gravity_flag = 0;
    p.y_vel = core::kJumpYVel;
    p.club_flag = 0;
    p.axe_flag = 0;
    state.halo_flight_flag = 0;  // power-ups drop on life loss
    p.energy = core::kInitialEnergy;
    p.hit_counter = 0x27;        // 39-frame respawn invulnerability
    p.hit_blink = 0;
    state.cave_entrance_mask = 0;
    state.cave_descent_third_shown = false;   // pairs with the mask reset
    // Restore the restart point's craft: a flight restart (saved at altitude)
    // respawns on the glider/balloon.  An engine-side guard with no EXE
    // counterpart; the reference clears the flag instead (divergence kept,
    // confirmed in playtest).
    state.glider_active = p.restart_glider;
    state.flash_frames = 0;        // engine cosmetic guard
    if (state.timer == 0) state.timer = 50;   // timer recovery
    if (state.timer < 11) state.timer += 10;
    state.frame_counter = 0;
}

}  // namespace olduvai::systems

namespace olduvai::systems {

void init_death_halo(SystemsState& state) {
    // Halo only when dying during balloon (L1) or glider (L5) flight.
    const bool halo_flag = state.glider_active && state.current_level == 1;
    const bool level5_flag = state.glider_active && state.current_level == 5;
    if (!halo_flag && !level5_flag) {
        state.death_halo_active = false;
        return;
    }
    state.death_halo_active = true;
    state.death_halo_x = state.player.x;
    state.death_halo_y = state.player.y - 30;
}

void tick_death_halo(SystemsState& state) {
    if (!state.death_halo_active) return;
    state.death_halo_y -= kBalloonRisePerTick;   // rises every frame
}

}  // namespace olduvai::systems
