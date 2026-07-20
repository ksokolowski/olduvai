// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/boss_l2.hpp"

#include <cstdio>
#include <cstdlib>

#include "core/rng.hpp"

namespace olduvai::systems {

namespace {

int lcg() { return static_cast<int>(core::global_rng().next()); }

// Diagnostic: with OLDUVAI_LOG_L2_SPAWN set, log each dino spawn side + the
// running right/left tally to stderr.  Lets a real playthrough report the
// actual distribution (the spawn-side decision is rand%100>50, EXE-faithful
// per FUN_23cf_06ec 0x0731-0x073a idiv-100; measured ~48% right over 200k
// frames — see Finding l2_dino_spawn_side_balanced.md).
void log_spawn_side(int direction) {
    static const bool on = std::getenv("OLDUVAI_LOG_L2_SPAWN") != nullptr;
    if (!on) return;
    static long right = 0, left = 0;
    if (direction == 1) ++right; else ++left;
    std::fprintf(stderr, "[L2-spawn] %-5s  running: right=%ld left=%ld (%.0f%% right)\n",
                 direction == 1 ? "RIGHT" : "left", right, left,
                 100.0 * right / static_cast<double>(right + left));
}

void update_stomp(L2BossState& boss) {
    ++boss.stomp_timer;
    if (boss.stomp_timer > 54) {
        boss.stomp_timer = 20 + lcg() % 30;   // randint(20, 49)
    }
}

bool check_hit(const BossPlayerState& p, const L2BossState& boss) {
    return !p.facing_left && p.y == kBossFloorY && p.x > 96 && p.x < 129 &&
           p.club_flag == 1 && boss.jaw_state == 0;
}

void check_knockback(BossPlayerState& p) {
    if (p.x > 120) {
        p.x -= 20;   // always push left
        if (p.hit_counter == 0 && p.knockback_stun == 0) {
            p.death_counter = 1;
            p.knockback_stun = 1;
            p.dead_counter = 1;
        }
    }
}

void update_projectiles(BossPlayerState& p, L2BossState& boss) {
    if (boss.cooldown != 0) --boss.cooldown;
    for (auto& slot : boss.slots) {
        if (slot.ptype > 2) continue;
        if (slot.ptype == 0) {
            if (boss.cooldown != 0) continue;
            if (lcg() % 100 > 50) {
                slot.direction = 1;
                slot.x = 360;
            } else {
                slot.direction = 0;
                slot.x = -40;
            }
            log_spawn_side(slot.direction);
            slot.ptype = 1;
            slot.frame = lcg() % 4;
            boss.cooldown = 30 + lcg() % 10;
            continue;
        }
        if (slot.ptype == 1) {
            slot.frame = (slot.frame + 1) & 3;
            int x = slot.x;
            if (slot.direction == 1) {
                x -= 4;
                if (x < -100) slot.ptype = 0;
            } else {
                x += 4;
                if (x > 400) slot.ptype = 0;
            }
            slot.x = x;
            // Club deflection — right-facing swing.
            if (!p.facing_left && p.club_flag == 1 && p.x + 40 > x &&
                p.x + 3 < x && p.y + 30 > kL2ProjY && p.y - 10 < kL2ProjY) {
                boss.sfx_hit_pending = true;
                slot.ptype = 2;
                slot.direction = 0;
            }
            // Club deflection — left-facing swing.
            if (p.facing_left && p.club_flag == 1 && p.x > x &&
                x + 36 > p.x && p.y + 30 > kL2ProjY &&
                p.y - 10 < kL2ProjY) {
                boss.sfx_hit_pending = true;
                slot.ptype = 2;
                slot.direction = 1;
            }
            // Damage AABB (only while still active).
            if (slot.ptype == 1) {
                if (p.x + 30 > x && p.x - 30 < x && p.y + 10 > kL2ProjY &&
                    p.y - 10 < kL2ProjY && p.knockback_stun == 0 &&
                    p.hit_counter == 0) {
                    p.death_counter = 1;
                    p.knockback_stun = 1;
                }
            }
            continue;
        }
        if (slot.ptype == 2) {   // deflected, bouncing away faster
            int x = slot.x;
            if (slot.direction == 1) {
                x -= 16;
                if (x < -100) slot.ptype = 0;
            } else {
                x += 16;
                if (x > 400) slot.ptype = 0;
            }
            slot.x = x;
        }
    }
}

}  // namespace

void update_l2_boss_frame(BossPlayerState& p, L2BossState& boss,
                          const BossInputs& in) {
    update_stomp(boss);
    if (check_hit(p, boss)) {
        boss.sfx_hit_pending = true;
        boss.arm_frame = 1;
        boss.jaw_state = 1;
    }
    check_knockback(p);
    update_boss_player(p, in.left, in.right, in.jump, in.fire);
    update_boss_club(p);
    if (p.x < 6) p.x = 6;
    if (p.halo_flag == 0) update_projectiles(p, boss);
    if (boss.health == kBossWinThreshold) boss.win_flag = true;   // JE exact
}

void tick_l2_boss_post_render(BossPlayerState& p, L2BossState& boss) {
    (void)p;
    // Jaw: idle stays; otherwise advance, and at the cycle end drain one
    // health point (the renderer erases the matching HUD pip column).
    if (boss.jaw_state != 0) {
        ++boss.jaw_state;
        if (boss.jaw_state >= 3) {
            boss.jaw_state = 0;
            if (boss.health > kBossWinThreshold) {
                --boss.health;
                boss.health_column_pending = true;
            }
        }
    }
    // Arm swing frames 1-5, then idle.
    if (boss.arm_frame != 0) {
        ++boss.arm_frame;
        if (boss.arm_frame > 5) boss.arm_frame = 0;
    }
}

}  // namespace olduvai::systems
