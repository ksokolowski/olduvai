// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/boss.hpp"

#include <algorithm>

namespace olduvai::systems {

namespace {

struct ClubBody { int spr, dy; };
constexpr ClubBody kClubBody[4] = {{4, -1}, {4, -7}, {3, 0}, {3, 0}};
struct ClubWeapon { int dx, dy, spr; };
constexpr ClubWeapon kClubWeapon[2] = {{14, -15, 13}, {20, 3, 6}};

void update_death(BossPlayerState& p, bool key_fire) {
    if (p.death_counter == 1) {
        p.dead_x_save = p.x;
        p.dead_anim_y = p.y;
    }
    if (p.death_counter < 9) {
        p.sprite = kBossDeathSpr[static_cast<std::size_t>(
            p.death_counter - 1)];
        p.sprite_dx = -5;
        p.sprite_dy = 0;
        p.x = p.dead_x_save;
        ++p.death_counter;
        return;
    }
    // Corpse rise: blink between the two rise frames, ascend 4/frame.
    p.sprite = kBossDeathRiseSpr + (p.dead_counter & 1);
    ++p.dead_counter;
    p.dead_anim_y -= 4;
    p.x = p.dead_x_save;
    p.y = p.dead_anim_y;
    p.sprite_dx = 0;
    p.sprite_dy = 0;
    if (key_fire) p.dead_anim_y = -1;   // skip the rise
    if (p.dead_anim_y > 0) return;
    // Respawn.
    p.x = p.respawn_x;
    p.y = p.respawn_y;
    p.hit_counter = kBossInvulnFrames;
    p.facing_left = false;
    p.walk_frame = 0;
    p.jump_counter = 0;
    p.club_flag = 0;
    p.dead_counter = 0;
    p.death_counter = 0;
    p.knockback_stun = 0;
    if (p.timer < 0x1E) p.timer += 0xA;   // timer recovery
    --p.lives;
}

}  // namespace

BossPlayerState init_boss_player(int lives, long score) {
    BossPlayerState p;
    p.x = 60;
    p.y = -40;
    p.halo_flag = kBossFlyInFrames;
    p.hit_counter = 0x27;   // invulnerable through the fly-in
    p.lives = lives;
    p.score = score;
    return p;
}

void update_boss_player(BossPlayerState& p, bool key_left, bool key_right,
                        bool key_jump, bool key_fire) {
    if (p.lives < 0) return;   // game over

    if (p.halo_flag > 0) {     // fly-in descent with wobble
        p.sprite = kBossFlyInSpr;
        p.sprite_dx = p.halo_flag & 1;
        p.y += 4;
        p.sprite_dy = -4;      // draw at the pre-increment y
        --p.halo_flag;
        p.facing_left = false;
        return;
    }
    if (p.death_counter > 0) {
        update_death(p, key_fire);
        return;
    }

    p.sprite = kBossStandSpr;
    p.sprite_dx = 0;
    p.sprite_dy = 0;
    bool fell = false;
    if (p.jump_counter == 0 && p.y < kBossFloorY) {
        fell = true;
        p.sprite = kBossAirSpr;
        for (int i = 0; i < 10; ++i) {
            ++p.y;
            if (p.y >= kBossFloorY) break;
        }
    }

    if (key_left && p.club_flag == 0) {
        int x_vel;
        if (!fell && p.jump_counter == 0) {
            p.walk_frame = (p.walk_frame + 1) % 6;
            p.sprite = kBossWalkSpr[static_cast<std::size_t>(p.walk_frame)];
            x_vel = kBossWalkXVel[static_cast<std::size_t>(p.walk_frame)];
            if (p.facing_left) {
                p.sprite_dx = -kBossWalkDx[static_cast<std::size_t>(
                    p.walk_frame)];
            }
            if (!p.facing_left) p.sprite = kBossTurnSpr;
        } else {
            x_vel = 5;
        }
        p.facing_left = true;
        p.x -= x_vel;
        if (p.jump_counter != 0) p.x -= 2;   // airborne slip
    } else if (key_right && p.club_flag == 0) {
        int x_vel;
        if (!fell && p.jump_counter == 0) {
            p.walk_frame = (p.walk_frame + 1) % 6;
            p.sprite = kBossWalkSpr[static_cast<std::size_t>(p.walk_frame)];
            x_vel = kBossWalkXVel[static_cast<std::size_t>(p.walk_frame)];
            if (!p.facing_left) {
                p.sprite_dx = kBossWalkDx[static_cast<std::size_t>(
                    p.walk_frame)];
            }
            if (p.facing_left) p.sprite = kBossTurnSpr;
        } else {
            x_vel = 5;
        }
        p.facing_left = false;
        p.x += x_vel;
        if (p.jump_counter != 0) p.x += 2;
    }

    if (p.jump_counter > 0) {
        for (int i = 0; i < 4; ++i) {
            if (p.jump_counter == 0) break;
            ++p.jump_counter;
            if (p.jump_counter == 2 && p.jump_peak > 0x14) {
                p.jump_apex_sfx_pending = true;
            }
            if (p.jump_counter > p.jump_peak - 8 &&
                p.jump_counter < p.jump_peak) {
                --p.y;
            }
            if (p.jump_counter < p.jump_peak - 6) p.y -= 2;
            if (p.jump_counter > p.jump_peak) {
                p.jump_counter = 0;
                fell = true;
                break;
            }
        }
        p.sprite = kBossAirSpr;
    }
    if (!fell && p.jump_counter == 0 && key_jump) {
        p.jump_counter = 1;
        p.jump_peak = kBossJumpPeak;
    }

    if (key_fire && p.club_flag == 0) p.club_flag = 2;
    if (p.club_flag > 0) {
        int idx = (p.jump_counter != 0 || fell) ? (-p.club_flag + 4)
                                                : (-p.club_flag + 2);
        idx = std::max(0, std::min(idx, 3));
        p.sprite = kClubBody[static_cast<std::size_t>(idx)].spr;
        p.sprite_dy = kClubBody[static_cast<std::size_t>(idx)].dy;
        if (key_left) {
            p.facing_left = true;
            p.x -= 5;
        } else if (key_right) {
            p.facing_left = false;
            p.x += 5;
        }
    }

    if (p.jump_counter == 0 && !fell && p.knockback_stun == 0) {
        p.respawn_x = p.x;
        p.respawn_y = p.y;
    }
    if (p.hit_counter > 0 && p.hit_counter < 100) {
        --p.hit_counter;
        p.halo_toggle = 1 - p.halo_toggle;
    }
}

void update_boss_club(BossPlayerState& p) {
    p.club_spr = -1;
    if (p.lives < 0) return;
    if (p.knockback_stun != 0 || p.halo_flag != 0) {
        p.club_flag = 0;   // cancel the attack
        return;
    }
    if (p.club_flag == 0) return;
    const auto& w = kClubWeapon[static_cast<std::size_t>(2 - p.club_flag)];
    p.club_spr = w.spr;
    p.club_dx = p.facing_left ? -w.dx : w.dx;
    p.club_dy = w.dy;
    --p.club_flag;
}

bool boss_player_hit(BossPlayerState& p) {
    // Invulnerability gates on hit_counter, not the fly-in flag.
    // EXE bug — matches original: boss hits are INSTANT KILL, not HP
    // subtraction.  L6 slam (FUN_254f_0078 0x012a-0x0148/0x0193-0x019c)
    // writes death_trigger + knockback_stun directly, never Game_HitPlayer.
    // Do NOT add an energy-subtraction path here.
    if (p.hit_counter > 0 || p.death_counter > 0) return false;
    p.death_counter = 1;
    p.dead_counter = 1;
    return true;
}

}  // namespace olduvai::systems
