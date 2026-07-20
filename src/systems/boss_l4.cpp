// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/boss_l4.hpp"

#include "systems/boss_l2.hpp"   // BossInputs

namespace olduvai::systems {

namespace {

void update_movement(L4BossState& boss) {
    if (boss.stun_counter != 0 || boss.win_flag == 2) return;
    if (boss.direction == 0) {
        boss.boss_x += 6;
        if (boss.boss_x > 270 && boss.win_flag != 3) {
            boss.boss_x = 240;
            boss.stun_counter = 1;
            boss.direction = 1;
        }
    } else {
        boss.boss_x -= 6;
        if (boss.boss_x < -10) {
            boss.boss_x = 10;
            boss.stun_counter = 1;
            boss.direction = 0;
        }
    }
    boss.walk_frame = (boss.walk_frame + 1) % 5;
}

void update_stun(L4BossState& boss) {
    if (boss.stun_counter > 0) boss.stun_counter = (boss.stun_counter + 1) & 3;
}

void update_spring(BossPlayerState& p, L4BossState& boss) {
    if (boss.spring_state == 0) {
        if (p.x > 139 && p.x < 165 && p.y == kBossFloorY &&
            p.knockback_stun == 0) {
            boss.spring_state = 1;
            p.jump_counter = 1;
            p.jump_peak = 40;   // higher than the regular jump
            p.y -= 15;
        }
    } else {
        p.y -= 15;
        boss.spring_state = (boss.spring_state + 1) & 3;
    }
}

bool check_hit(const BossPlayerState& p, const L4BossState& boss) {
    // Tail-side club hit; stun does NOT block it.
    if (p.club_flag != 1) return false;
    if (p.y <= 130) return false;
    if (boss.direction == 0) {
        return !p.facing_left && p.x + 30 > boss.boss_x - 80 &&
               p.x < boss.boss_x - 55;
    }
    return p.facing_left && boss.boss_x + 105 < p.x + 30 &&
           p.x < boss.boss_x + 145;
}

void check_knockback(BossPlayerState& p, const L4BossState& boss) {
    if (p.knockback_stun != 0) return;
    if (p.hit_counter != 0) return;
    if (p.y <= 130) return;
    const bool in_zone =
        (boss.direction == 0)
            ? (boss.boss_x - 78 < p.x && p.x < boss.boss_x + 50)
            : (boss.boss_x - 25 < p.x && p.x < boss.boss_x + 90);
    if (in_zone) boss_player_hit(p);
}

void update_victory(BossPlayerState& p, L4BossState& boss) {
    if (boss.win_flag == 1) {
        update_movement(boss);
        update_stun(boss);
        if (boss.direction == 0 && boss.boss_x - 60 < p.x &&
            p.x < boss.boss_x - 20) {
            boss.win_flag = 2;
        }
    } else if (boss.win_flag == 2) {
        p.y -= 8;
        if (p.y < 120) {
            boss.win_flag = 3;
            p.y = 119;
        }
    } else if (boss.win_flag == 3) {
        update_movement(boss);
        if (boss.boss_x > 360) boss.win_flag = 100;   // ride off complete
    }
}

}  // namespace

void update_l4_boss_frame(BossPlayerState& p, L4BossState& boss,
                          const BossInputs& in) {
    if (boss.win_flag != 0) {
        update_victory(p, boss);
        return;
    }
    update_movement(boss);
    update_stun(boss);
    if (boss.hit_flash > 0) --boss.hit_flash;
    if (p.halo_flag == 0 && p.death_counter == 0) update_spring(p, boss);
    update_boss_player(p, in.left, in.right, in.jump, in.fire);
    update_boss_club(p);
    if (p.x < 10) p.x = 10;
    else if (p.x > 280) p.x = 280;
    if (check_hit(p, boss)) {
        boss.sfx_hit_pending = true;
        boss.hit_flash = 6;
        boss.health = std::max(kBossWinThreshold, boss.health - 2);
    }
    check_knockback(p, boss);
    if (boss.health <= kBossWinThreshold) {
        boss.win_flag = 1;
        p.y = kBossFloorY;           // FUN_24cc_02f2 +0x06c7 (0xA6 = 166)
        p.facing_left = false;       // +0x06cd
        // Victory-path player_x clamp — FUN_24cc_02f2 +0x06a2..0x06b7
        // ([0x14, 0xDC] = [20, 220]; distinct from the fight clamp
        // [10, 280] at +0x0663).  The phase-1 mount window
        // (boss_x-60, boss_x-20) tops out below x=250; without this clamp
        // a far-right player is unreachable and the boss patrols forever
        // (softlock hit in playtest 2026-07-06).
        if (p.x > 0xDC) p.x = 0xDC;
        if (p.x < 0x14) p.x = 0x14;
    }
}

}  // namespace olduvai::systems
