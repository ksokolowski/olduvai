// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Player state and physics, headless.  The original state machine:
// walking (6-frame cycle), counter-based integer gravity, two-probe ground
// detection, club attack with an edge-trigger latch, post-hit
// invulnerability, death animation + ghost float, respawn.
//   state machine          // FUN_27f7_1b51
//   damage                 // FUN_27f7_18d4
//   death / respawn        // FUN_27f7_1921
//   invulnerability tick   // FUN_27f7_12c7

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/collision_bitmap.hpp"
#include "core/constants.hpp"
#include "core/types.hpp"

namespace olduvai::systems {

// Floating score popup slot (10-slot table).  // FUN_27f7_0798, DS:0x9806
struct ScoreBonus {
    int counter = 0;   // 0 = inactive
    int x = 0, y = 0;
    int sprite = 0;
    int prev_x = 0, prev_y = 0;
    float fx = 0.0f, fy = 0.0f;   // float render shadow (smooth-motion HD path)
    bool active_this_frame = false;
};

// Player physics constants (1-based sprite values converted to 0-based).
constexpr int kSpringYVel = 162;        // 0xA2 — bit 7 = spring marker
constexpr int kMaxFallSearch = 10;
constexpr int kMaxGravitySubsteps = 4;
constexpr int kProbeLeftX = 8;
constexpr int kProbeRightX = 23;
constexpr int kProbeFootY = 29;
constexpr std::array<int, 6> kWalkSpr = {0, 1, 96, 0, 2, 97};
constexpr std::array<int, 6> kWalkVel = {4, 6, 6, 4, 6, 6};
constexpr std::array<int, 6> kWalkDx = {0, -6, -5, 0, -6, -5};
constexpr std::array<int, 8> kDeathSpr = {7, 7, 100, 101, 102, 103, 103, 103};
struct HitFrame { int spr, dy; };
constexpr std::array<HitFrame, 4> kHittingSpr = {{{4, -1}, {4, -7}, {3, 0}, {3, 0}}};
constexpr int kSprPlayerStand = 0;
constexpr int kSprPlayerJump = 3;
constexpr int kSprPlayerClimb1 = 15;
constexpr int kSprPlayerGhost1 = 38;
constexpr int kSprPlayerTurn = 134;

struct PlayerState {
    int x = 40;               // DS:0x988e
    int y = 100;              // DS:0x97f2
    int prev_x = 40, prev_y = 100;  // smooth-motion render interp only
    // The sub-pixel render position lives on RenderTarget, not here:
    // PlayerState is memcpy'd into the POD SaveHeader, so a new field changes
    // the save layout.
    int prev_dx = 0, prev_dy = 0;
    int y_vel = core::kJumpYVel;    // DS:0x97f4
    int gravity_flag = 0;     // DS:0x9868 — 0 grounded, >0 ascending counter
    int facing_left = 0;      // DS:0x9800
    int walk_frame = 0;       // DS:0x9892 — 0..5
    int dx = 0;               // DS:0x986e — draw offset
    int dy = 0;               // DS:0x985e
    int club_flag = 0;        // DS:0x9862 — 0 idle, 1-2 swing phase
    int attack_latch = 0;     // DS:0x97ec
    int axe_flag = 0;         // DS:0x97ea
    int climbing = 0;         // DS:0x97e2
    int climb_y_min = 0;
    int climb_y_max = 200;
    int cave_warp_freeze = 0; // DS:0x987e
    // Render-only fade signal for cave-warp screen changes that do not go
    // through cave_warp_freeze's 0x3E8 path (L3 11->12 trunk-cave exit, the
    // screen-9 cave entry).  The EXE fades these via bp-6=0 ->
    // Sprite_DrawDispatch mode 2 (FUN_1052_0c15).  Set in the transition
    // branch, cleared by the screen-change classifier; no RNG or logic effect.
    bool cave_warp_pending = false;
    int halo_spr = 0;
    int hit_counter = 0;      // DS:0x9874
    int hit_blink = 0;        // DS:0x9c6a
    int energy = core::kInitialEnergy;  // DS:0x989a
    int platform_flag = 0;    // DS:0x97e6
    int death_counter = 0;    // DS:0x9870
    int ghost_rise = 0;       // DS:0x97fa
    int death_save_x = 0;     // DS:0x9c60
    int death_save_y = 0;     // DS:0x97de
    int lives = core::kInitialLives;    // DS:0x9c5e
    int restart_x = 40;       // DS:0x9c64
    int restart_y = 100;      // DS:0x97fc
    int restart_screen = 0;   // DS:0x9866
    int restart_cave_index = -1;
    int restart_secret_index = -1;
    // The restart point was saved in flight (L5 glider / L1 balloon): respawn
    // puts the player back on the craft at altitude (restart_y=100), as the EXE
    // does; otherwise they would fall to certain death.  Grounded saves clear
    // it.
    bool restart_glider = false;
    int saved_y_vel = 0;      // DS:0x9c70
    int sprite = kSprPlayerStand;

    void reset_for_level(int start_x = 40, int start_y = 100);
};

struct InputState {
    bool left = false, right = false, jump = false, down = false,
         attack = false;
};

// The slice of game state the player systems touch.
struct SystemsState {
    PlayerState player;
    InputState input;
    core::CollisionBitmap collision;
    int current_level = 1;
    int current_screen = 0;
    int screen_height = 199;
    bool screen_change = false;
    bool game_over = false;
    int frame_counter = 0;
    int timer = 99;
    // Cave/secret render-mode flags (the original folds them into
    // current_screen >= 100).
    int cave_flag = 0, cave_index = -1;
    int cave_return_screen = 0, cave_return_x = 0, cave_return_y = 0;
    int secret_flag = 0, secret_index = -1;
    int secret_return_screen = 0, secret_return_x = 0, secret_return_y = 0;
    int cave_entrance_mask = 0;
    bool glider_active = false;
    int glider_x = 0, glider_y = -100;   // detached-glider fly-away anim
    int halo_flight_flag = 0;  // DS:0x97f8 — "axe-powered" power-up
    bool god_mode = false;
    int flash_frames = 0;
    // Cave-emerge countdown, an intentional divergence: after exit_cave the
    // player shows kSprPlayerTurn (134, front-facing; FUN_27f7_1b51 0x1da1).
    // The DOS EXE has no emerge frames (no DS:0x987e re-arm on exit); the Amiga
    // port does.  Enhanced: 9 ticks, 3 dim stages x 3, player frozen
    // (frame_runner); a hit or death cancels it.  Classic: 2 lit ticks, draw
    // only, EXE timing. Transient.  Decremented at the end of the tick, after
    // every present path has shown the value.
    int cave_emerge_frames = 0;
    // Third-descent-frame latch (frame 46; see tick_cave_descent).  Transient.
    bool cave_descent_third_shown = false;
    // GameOptions::enhanced, for render-only cosmetic gates.
    bool enhanced_active = false;
    // run_tick: an Enhanced animation stopped game time this tick, so the
    // draw-timed game counters (tick_get_ready) wait with the world.
    bool time_stopped = false;
    // Enhanced teleport clouds (cave signs).  Departure: clouds shrink at the
    // sign, player hidden, teleport deferred (pending fields hold the
    // destination). Arrival: clouds grow, then the player appears in the EXE's
    // own 0x27-tick halo.  Armed by collision_dispatch, ticked and drawn by
    // game_app. Transient.
    int teleport_out_ticks = 0;
    int teleport_in_ticks = 0;
    bool pending_sign_teleport = false;
    int pending_tel_screen = 0, pending_tel_x = 0, pending_tel_y = 0;
    int teleport_fx_x = 0, teleport_fx_y = 0;
    bool jump_apex_sfx_pending = false;
    // ── entity + scoring state (collision dispatch / frame loop) ──
    std::vector<core::Entity> entities;
    long score = 0;
    long next_life_score = 10000;    // hiscore counter (DS:0x97ee/0x97f0)
    int food_count = 0;              // DS:0x9880
    int bonus_trigger = 0;           // DS:0x9894 — bomb kill-all flag
    int timer_counter = 0;           // DS:0x985a — HUD flash frames
    int l3a_phase_counter = 0;       // DS:0x8000
    // Fire-monster fireball (one in flight).  // Game_UpdateFireball
    int fireball_flag = 0;           // 0 none, 1 right, 2 left
    int fireball_x = 0, fireball_y = 0;
    std::array<ScoreBonus, 10> score_bonuses{};
    bool transition_skip = false;    // skip player physics this frame
    bool level_complete = false;
    int get_ready_counter = 0x11;   // DS:0x97e0 — GET READY banner
    bool skip_player_update = false; // descent/teleport owns the player this frame
    // Pending SFX events (consumed by the audio layer once it exists).
    bool sfx_hit_pending = false;      // club hit (MASSUE2-class)
    bool sfx_generic_pending = false;  // score/bonus ding
    bool sfx_spring_pending = false;   // trampoline/lava spring
    // DS:0x9860 — true only when every monster slot is permanently dead.
    bool screen_clear_of_monsters = true;
    // Rolling stone (one per screen).  // FUN_27f7_089f
    // Death halo (rises 8 px/frame on a balloon/glider death).
    // Dual-purpose DS slots 0x9876 / 0x9804.   // FUN_27f7_1921
    bool death_halo_active = false;
    int death_halo_x = 0, death_halo_y = 0;
    int stone_state = 0;               // DS:0x97e4 — 0 off, 1 right, 2 left
    int stone_x = 0;                   // DS:0x97f6
    int stone_y = 0;                   // DS:0x9884
    // Previous-tick positions for smooth-motion interpolation only.
    int prev_stone_x = 0, prev_stone_y = 0;
    int prev_fireball_x = 0, prev_fireball_y = 0;
    int prev_glider_x = 0, prev_glider_y = -100;
    int prev_death_halo_x = 0, prev_death_halo_y = 0;
    // Float render shadows for the smooth HD path; render only, not saved.
    float stone_fx = 0.0f, stone_fy = 0.0f;
    float fireball_fx = 0.0f, fireball_fy = 0.0f;
    float death_halo_fx = 0.0f, death_halo_fy = 0.0f;
    float glider_fx = 0.0f, glider_fy = -100.0f;   // L5 screen-12 detached glider
    // L3 descent smoke jitter: 20 (a, b) pairs, each 0..9, pre-rolled from the
    // global LCG at the reference's logic moment (FUN_2276_03d9 iters 0..19),
    // before Phase 2; read-only for the renderer.
    std::array<std::pair<int,int>, 20> l3_descent_smoke_jitter{};
    // Set on the frame the secret-room trampoline fired; the spring draws
    // raised (y 148 instead of 164).
    bool secret_spring_bouncing = false;
    // Player x when check_secret_exit fired (before the return x overwrites
    // it): the start of the enhanced exit slide's arc.
    int secret_exit_x = 0;
};

// Per-frame invulnerability tick (independent of the player-update path).
void tick_post_hit_invuln(PlayerState& p);

void update_player(SystemsState& state);
void clamp_player_position(SystemsState& state);   // x → [0, 300]
void check_death_by_fall(SystemsState& state);     // y > 180 → death
void update_climbing(SystemsState& state);
void update_death(SystemsState& state);
void trigger_death(SystemsState& state);
void hit_player(SystemsState& state, int damage = 2);
void respawn(SystemsState& state);
// Balloon bunch rise per tick: the death halo (FUN_27f7_1921, 8 px) and the
// enhanced fly-away after a landing.
constexpr int kBalloonRisePerTick = 8;
void init_death_halo(SystemsState& state);   // death_counter == 1
void tick_death_halo(SystemsState& state);   // death_counter > 1

}  // namespace olduvai::systems
