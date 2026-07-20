// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// L2 T-Rex boss logic.                       // FUN_23cf_0a20 main
// Jaw bite state machine (health drain at cycle end, win at health ==
// 272 exactly — JE, not JLE)                  // FUN_23cf_056b
// Arm swing frames 1-5                        // FUN_23cf_0653
// 4-slot rock-projectile array with spawn cooldown, club deflection
// (both facings) and the damage AABB           // FUN_23cf_06ec
// Knockback push past x>120 with the stun+death write.
//
// Per-frame order (main loop): stomp → hit-check/apply → knockback →
// player update → club → x>=6 clamp → projectiles (post fly-in) →
// win check; jaw + arm advance AFTER the render.

#pragma once

#include <array>

#include "systems/boss.hpp"

namespace olduvai::systems {

struct L2ProjectileSlot {
    int ptype = 0;       // 0 spawn, 1 active, 2 dying
    int direction = 0;   // 0 right, 1 left
    int x = 0;
    int frame = 0;
    int prev_x = 0;
    float fx = 0.0f;   // float render shadow (smooth-motion HD path)
};

struct L2BossState {
    int health = kBossHealthStart;       // win at 272 (exact)
    int jaw_state = 0;                   // 0 idle, 1 closing, 2 open
    int stomp_timer = 40;
    int arm_frame = 0;                   // 0 idle, 1-5 swing
    bool win_flag = false;
    std::array<L2ProjectileSlot, 4> slots{};
    int cooldown = 0;                    // DS:0x9328
    bool sfx_hit_pending = false;
    bool health_column_pending = false;  // erase HUD pip column at `health`
};

constexpr std::array<int, 4> kL2ProjAnim = {0, 1, 0, 2};
constexpr int kL2ProjY = 164;
constexpr int kL2ProjSprRight = 40, kL2ProjSprLeft = 44;
constexpr int kL2ProjSprDyingLeft = 41, kL2ProjSprDyingRight = 45;

struct BossInputs { bool left = false, right = false, jump = false,
                    fire = false; };

// One frame of L2 boss logic (pre-render part).
void update_l2_boss_frame(BossPlayerState& p, L2BossState& boss,
                          const BossInputs& in);
// Post-render part: jaw + arm advance.
void tick_l2_boss_post_render(BossPlayerState& p, L2BossState& boss);

}  // namespace olduvai::systems
