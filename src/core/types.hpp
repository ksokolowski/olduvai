// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Core gameplay data types — no SDL, no I/O.
//
// Field naming: semantic primary names; data-table fields keep their
// register-style suffixes where the evidence is keyed to them.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace olduvai::core {

// Monster state machine.  // FUN_27f7_093d
// No state 2 — the original skips that value; the gap is deliberate.
enum class MonsterState : std::uint8_t {
    Reset = 0,
    Spawn = 1,
    HeadingPlayer = 3,
    RunningAway = 4,
    Ko = 5,
    Dead = 6,
};

// Object type IDs — 39-entry dispatcher.  // FUN_2A04_0003 jump table
enum class ObjType : std::uint8_t {
    Stairs = 1,
    Peak = 2,             // bounce pad (spring trampoline)
    Egg = 3,
    Rock = 4,
    AncestorGhost = 5,    // drops a random power-up when clubbed
    HiddenFood = 6,
    SecretFood = 7,
    Balloons = 8,
    Fish = 9,
    RedDino = 10,         // monster class A
    YellowFuzz = 11,      // monster class B
    BrownBear = 12,       // monster class C
    GreenDino = 13,       // monster class D
    Chimp = 14,           // throw-arc type
    Bird = 15,            // two-entity (body + swooper)
    Platform = 16,
    CaveEntrance = 18,
    Fire = 19,
    FoodCave = 20,
    CaveSign = 21,
    CaveSpider = 22,
    CaveBat = 23,
    AnimatedFoodL3 = 24,
    VineL3 = 25,
    MonsterL3A = 26,
    MonsterL3B = 27,
    BreakableRockL3 = 28,
    SnakeL3 = 29,
    ProjectileL3 = 30,    // launcher + projectile hazard
    JumpingFishL5 = 31,
    ChimpL5 = 32,         // snowman (chimp handler)
    MonsterL5A = 33,
    MonsterL5B = 34,
    PteriyakiL7 = 35,     // lava bubbles (bird handler)
    ChimpL7 = 36,
    PeakL7 = 37,          // lava spring
    MonsterL7A = 38,
    MonsterL7B = 39,
};

// A live game entity (monster / collectible / hazard / platform).
struct Entity {
    ObjType obj_type = ObjType::Stairs;
    int x = 0, y = 0;
    // Smooth-motion enhancement only (render interpolation; never read by
    // game logic).
    int prev_x = 0, prev_y = 0;
    // Sub-pixel render position written by the smooth-motion lerp and read by
    // draw_entities ONLY when RenderTarget::use_float_pos is set (HD path) —
    // gives Python's 1-HD-pixel motion granularity instead of the integer
    // lerp's 4-HD-pixel snap.  Untouched (and unread) on every other path.
    float fx = 0.0f, fy = 0.0f;
    // Float render shadows for the other interpolated per-entity fields
    // (moving-platform current_y, chimp throw, spider/snowman bob) — same HD
    // smooth-motion path, same use_float_pos gate.
    float f_current_y = 0.0f;
    float f_throw_x = 0.0f, f_throw_y = 0.0f;
    float f_draw_dy = 0.0f;
    int prev_current_y = 0;              // moving platform
    int prev_throw_x = 0, prev_throw_y = 0;   // chimp projectile
    int prev_draw_dy = 0;                // spider/snowman bob
    // Common state
    int state = 0;
    int sprite = 0;
    bool facing_left = false;
    bool active = true;
    bool visible = true;
    int frame_counter = 0;
    // Monster
    int init_x = 0, init_y = 0;
    int init_state = 0;
    int respawns = 0;
    int direction = 0;
    int state_counter = 0;
    int ko_counter = 0;
    // Platform
    int y_top = 0, y_bottom = 0;
    int dy = 0;
    int current_y = 0;
    // Chimp throw
    int throw_flag = 0, throw_x = 0, throw_y = 0;
    // Fire monster fireball request (0 = none, 1 = right, 2 = left)
    int fireball_request = 0;
    // Generic
    int counter = 0;
    int spr_num = 0;
    int mask = 0;
    // Monster data-table animation/collision fields
    std::vector<int> walk_offsets;
    int init_spr = 0;
    int away_spr = 0;
    int ko_spr = 0;
    int hits_to_ko = 1;
    int dat00 = 16;            // collision X offset (arrow position)
    int probe_di = 20;         // forward-probe X offset
    int probe_si = 30;         // foot-probe Y offset
    int club_reach_right = 30; // dat02 — player facing right
    int club_reach_left = 28;  // var16 — player facing left
    int direction_flag = 0;
    // L3 animation-phase alternates (-1 / empty = unused)
    int alt_spr_num = -1;
    int alt_away_spr = -1;
    std::vector<int> alt_walk_offsets;
    // Bird
    std::array<int, 4> bird_heights{};
    int bird_height_idx = 0;
    int bird_anim = 0;
    // Off-screen-left despawn bound for the bird (EXE: x < -50 = off the 320
    // screen).  Default keeps that faithful value; the WIDESCREEN render path
    // lowers it to the wide-margin edge so the bird flies fully off the strip
    // instead of vanishing inside it.  Render-coupled (set only when widescreen
    // is active) — classic stays byte-identical.
    int off_screen_left = -50;
    // Off-screen-RIGHT spawn x for the bird (EXE: 355 = off the 320 screen, so
    // it flies IN).  In widescreen x=355 lands inside the right margin → it pops
    // in; the WIDESCREEN render path raises it past the wide edge so it flies in
    // continuously.  Same gating as off_screen_left (classic byte-identical).
    int bird_spawn_x = 355;
    // Spider
    int draw_dy = 0;
    int spider_di = 0;
    // Projectile launcher
    int launcher_spr = -1;
    int proj_x = 0, proj_y = 0;
    // Collision box
    int hit_w = 0, hit_h = 0;
    // Platform oscillation speed (per-record)
    int speed = 1;
    // Snake body rendering
    int body_sprite = -1;
    int body_x = 0, body_y = 0;
    int head_x = 0, head_y = 0;
    // Pterodactyl L7 dual-sprite animation
    int wing_x = 0;
    int wing_phase = 0x4D;
    int wing_subcounter = 0;
    int body_phase = 0x38;
    int body_subcounter = 0;
    // Scatter anchors (chimp L7 / pteriyaki L7)
    int anchor_a = 0, anchor_b = 0;
    // Ancestor-ghost rising-bonus animation
    int bonus_rise_dy = 0;
    int bonus_rise_y = 0;
    bool bonus_pending = false;
};

// Result of one frame's player-entity collision pass.
struct CollisionResult {
    int damage = 0;
    int food_collected = 0;
    int score_gained = 0;
    struct ScoreEvent { int x, y, value; };
    std::vector<ScoreEvent> score_events;
    bool spring_bounce = false;
    bool monster_hit = false;
    int climb_x = 0, climb_y_top = 0, climb_y_bottom = 0;
    bool can_climb = false;
    bool climb_exit_top = false, climb_exit_bottom = false;
    int climb_exit_y = 0;
    int cave_enter = -1;
    int cave_sign_screen = -1;
    int cave_sign_x = 0, cave_sign_y = 0;
    int platform_y = -1;
    int bonus_type = -1;
    // Lava-spring launch (distinct from spring_bounce; type-specific formula)
    bool peak_l7_spring = false;
    int peak_l7_x_delta = 0, peak_l7_y_delta = 0, peak_l7_y_vel = 0;
};

}  // namespace olduvai::core
