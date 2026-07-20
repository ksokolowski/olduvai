// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// L4 Triceratops boss logic.                  // FUN_24cc_02f2 main
// Patrol movement (+-6/frame, edge turns with a 1-cycle stun), the
// tail-side club hit (-2 health, clamped at the 272 win threshold),
// direction-dependent knockback zones, the back-spring launch zone
// (139 < x < 165 at the floor), and the three-phase victory ride
// (enter the zone -> rise to y=119 -> the boss walks off right).

#pragma once

#include "systems/boss.hpp"

namespace olduvai::systems {

struct L4BossState {
    int health = kBossHealthStart;   // win at 272
    int boss_x = 180;                // DS:0x933c
    int boss_y = 136;                // DS:0x9332
    int prev_x = 180, prev_y = 136;
    float fx = 180.0f, fy = 136.0f;  // float render shadow (smooth-motion HD)
    int walk_frame = 0;              // 0-4
    int direction = 0;               // 0 right, 1 left
    int stun_counter = 0;            // cycles &3
    int hit_flash = 0;
    int spring_state = 0;            // cycles &3 while active
    int win_flag = 0;                // 0 fight, 1-3 victory, 100 done
    bool sfx_hit_pending = false;
};

void update_l4_boss_frame(BossPlayerState& p, L4BossState& boss,
                          const struct BossInputs& in);

}  // namespace olduvai::systems
