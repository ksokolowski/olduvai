// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Shared boss-arena framework: the boss player.   // FUN_23cf_018b
// Self-contained physics — no collision bitmap, fixed floor at y=166,
// fly-in descent (wobbling fall sprite, 42 frames), simplified gravity,
// counter-based jump with the peak at 0x11, club with airborne frames,
// death-and-rise corpse animation with respawn.    // FUN_23cf_0096
// Club weapon overlay + cancellation guards:       // FUN_23cf_045f

#pragma once

#include <array>

namespace olduvai::systems {

constexpr int kBossFloorY = 0xA6;        // 166
constexpr int kBossHealthStart = 0x13D;  // 317
constexpr int kBossWinThreshold = 0x110; // 272 → 45 hits to win
constexpr int kBossInvulnFrames = 0x27;  // 39
constexpr int kBossFlyInFrames = 42;
constexpr int kBossJumpPeak = 0x11;

constexpr std::array<int, 8> kBossDeathSpr = {7, 7, 22, 23, 24, 25, 25, 25};
constexpr std::array<int, 6> kBossWalkSpr = {0, 1, 18, 0, 2, 19};
constexpr std::array<int, 6> kBossWalkXVel = {4, 6, 6, 4, 6, 6};
constexpr std::array<int, 6> kBossWalkDx = {0, -6, -5, 0, -6, -5};
constexpr int kBossFlyInSpr = 48;
constexpr int kBossTurnSpr = 28;
constexpr int kBossAirSpr = 3;
constexpr int kBossStandSpr = 0;
constexpr int kBossDeathRiseSpr = 16;    // + (dead_counter & 1)
constexpr int kBossHaloSprBase = 26;     // invuln flash sprites 26/27

struct BossPlayerState {
    int x = 60;
    int y = -40;                  // starts off-screen (fly-in)
    int prev_x = 60, prev_y = -40;
    bool facing_left = false;
    int walk_frame = 0;
    int jump_counter = 0;
    int jump_peak = kBossJumpPeak;
    int club_flag = 0;            // DS:0x9862
    int death_counter = 0;        // DS:0x97fa — 0 alive, 1-8 dying, 9+ corpse
    int dead_counter = 0;
    int dead_anim_y = 0;
    int dead_x_save = 0;
    int respawn_x = 150;
    int respawn_y = kBossFloorY;
    int halo_flag = kBossFlyInFrames;   // DS:0x97e8 — fly-in counter here
    int hit_counter = kBossInvulnFrames; // DS:0x9874
    int halo_toggle = 0;          // DS:0x9c6a — invuln flash blink
    int knockback_stun = 0;       // DS:0x9870
    int timer = 0;                // DS:0x9c66
    int lives = 3;
    long score = 0;
    int sprite = kBossStandSpr;
    int sprite_dx = 0, sprite_dy = 0;
    int club_spr = -1;            // -1 = no club sprite this frame
    int club_dx = 0, club_dy = 0;
    bool jump_apex_sfx_pending = false;
};

BossPlayerState init_boss_player(int lives = 3, long score = 0);

void update_boss_player(BossPlayerState& p, bool key_left, bool key_right,
                        bool key_jump, bool key_fire);

// Club weapon overlay; call after update_boss_player.
void update_boss_club(BossPlayerState& p);

// Returns true when the hit actually started the death sequence.
bool boss_player_hit(BossPlayerState& p);

}  // namespace olduvai::systems
