// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Diagnostic: count L2 boss dino spawn sides over many frames, driving the
// REAL update_l2_boss_frame logic + the real global LCG.  Measures whether the
// spawn side is balanced (owner reported ~95% right).
#include <cstdio>
#include <cstdlib>

#include "systems/boss.hpp"
#include "systems/boss_l2.hpp"

using namespace olduvai::systems;

int main(int argc, char** argv) {
    const long frames = argc > 1 ? std::atol(argv[1]) : 200000;
    BossPlayerState p = init_boss_player();
    L2BossState boss;
    BossInputs in;  // neutral

    int prev[4] = {0, 0, 0, 0};
    long right = 0, left = 0;
    for (long f = 0; f < frames; ++f) {
        // Park the player out of the death/knockback zone + permanent invuln so
        // the fight never ends; enable spawns (halo_flag fly-in done).
        p.x = 60;
        p.halo_flag = 0;
        p.hit_counter = 999;
        p.death_counter = 0;
        p.knockback_stun = 0;
        for (int i = 0; i < 4; ++i) prev[i] = boss.slots[i].ptype;
        update_l2_boss_frame(p, boss, in);
        for (int i = 0; i < 4; ++i) {
            if (prev[i] == 0 && boss.slots[i].ptype == 1) {
                if (boss.slots[i].direction == 1) ++right;  // x=360, right edge
                else ++left;                                // x=-40, left edge
            }
        }
    }
    const long tot = right + left;
    std::printf("L2 dino spawns / %ld frames: right(dir1,x360)=%ld "
                "left(dir0,x-40)=%ld  (%.1f%% right)\n",
                frames, right, left, tot ? 100.0 * right / tot : 0.0);
    return 0;
}
