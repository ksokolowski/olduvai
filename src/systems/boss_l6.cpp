// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/boss_l6.hpp"

#include "systems/boss_l2.hpp"   // BossInputs

namespace olduvai::systems {

namespace {

void update_ground_punch(L6BossState& boss, BossPlayerState& p) {
    if (boss.ground_punch_state == 0 && p.x > 99 && p.x < 125 &&
        p.y == 166 && p.knockback_stun == 0) {
        boss.ground_punch_state = 1;
        p.jump_counter = 1;
        p.jump_peak = 70;   // huge launch
    }
    if (boss.ground_punch_state > 0) {
        p.y = std::max(p.y - 15, -40);
        boss.ground_punch_state = (boss.ground_punch_state + 1) & 3;
    }
}

void advance_frame_counter(L6BossState& boss, bool smooth_motion) {
    // Hit-reaction hold: while the shocked pair is visible the counter
    // FREEZES at 19 so the QoL hold does not consume the EXE's post-hit
    // tail.  EXE (capstone 254f_0078): hit tick sets [0x9348]=0x13=19
    // (:01c3) and inline-draws the shocked pair (:01c9-0201); the next
    // tick's `inc [0x9348]` (:0078) lands on 20 → the window draw
    // (:00a3-00f7) blits the base+0/base+3 pair — the animated return of
    // the arm+head strip.  The engine plays that tail on the first tick
    // after the hold expires.  With the EXE-equivalent hold of 1 the
    // counter was already decremented to 0 post-render on the hit tick,
    // so this branch never fires and the tick sequence is identical to
    // the EXE's.  Mirrors reference boss_l6.py::_advance_frame_counter.
    if (boss.hit_reaction_counter > 0) return;
    // Smooth-motion (enhanced) holds each slam pose in the attack window
    // [16,20] for 2 logic ticks: skip the advance on alternate calls so the
    // swing/impact/recovery cycle reads at ~9 Hz instead of 18 Hz.  Damage
    // still fires on frame_counter 17/18 (the held tick is blocked from a
    // double-hit by the p.hit_counter>0 guard in check_slam_damage).  Mirrors
    // reference boss_l6.py::_advance_frame_counter.  Classic mode unchanged.
    if (smooth_motion && boss.frame_counter >= 16 && boss.frame_counter <= 20) {
        boss.slam_slow_tick ^= 1;
        if (boss.slam_slow_tick == 1) return;
    }
    boss.frame_counter = (boss.frame_counter + 1) & 0x1F;
    if (boss.frame_counter == 0) boss.frame_counter = 7;
}

bool is_attack_frame(const L6BossState& boss) {
    return boss.frame_counter >= 16 && boss.frame_counter <= 20;
}

bool check_player_hit(const L6BossState& boss, const BossPlayerState& p) {
    if (is_attack_frame(boss)) return false;
    return p.club_flag == 1 && p.y < 50 && p.x > 182;
}

// Slam = instant kill; there is no health-damage path here.
bool check_slam_damage(const L6BossState& boss, BossPlayerState& p) {
    if (p.knockback_stun != 0 || p.hit_counter > 0) return false;
    const int fc = boss.frame_counter;
    if (fc == 17) {
        if (p.x > 120 && p.x < 180 && p.y < 40) return boss_player_hit(p);
        if (p.x > 100 && p.x < 130) return boss_player_hit(p);
    } else if (fc == 18) {
        if (p.y > 120 && p.y < 130 && p.x > 92 && p.x < 150) {
            return boss_player_hit(p);
        }
    }
    return false;
}

}  // namespace

void update_l6_boss_frame(BossPlayerState& p, L6BossState& boss,
                          const BossInputs& in, bool smooth_motion) {
    update_ground_punch(boss, p);
    advance_frame_counter(boss, smooth_motion);
    if (check_player_hit(boss, p)) {
        boss.sfx_hit_pending = true;
        boss.health = std::max(boss.health - 2, kBossWinThreshold);
        boss.frame_counter = 19;
        boss.hit_reaction_counter = 3;
    }
    if (p.hit_counter == 0) check_slam_damage(boss, p);
    update_boss_player(p, in.left, in.right, in.jump, in.fire);
    update_boss_club(p);
    p.x = std::max(10, std::min(p.x, 280));
    if (boss.health <= kBossWinThreshold) boss.win_flag = 1;
}

void tick_l6_boss_post_render(L6BossState& boss) {
    if (boss.hit_reaction_counter > 0) --boss.hit_reaction_counter;
}

// Two-part H-atlas composite selection.  EXE FUN_254f_0078 draws TWO
// Sprite_DrawMultiBuffer (FUN_1052_2b53) parts from the contiguous atlas
// based at DS:0x9896 (H1..H4.MAT load at base+0..base+3, FUN_254f_02b5
// capstone 0x02f6-0x0350, so the H4 sheet spans base+3..base+6):
//   swing window (16..20, :00a3-00f7):
//     part A body     x=0x50=80:  frame = tbl[c-16] + base      (:00b4-:00b8)
//     part B arm+head x=0xd0=208: frame = tbl[c-16] + base + 3  (:00de-:00e6)
//     tbl = DS:0x1fe4 = {0,1,2,1,0} (raw EXE bytes at file 0x21974:
//     00 00 01 00 02 00 01 00 00 00)
//   hit reaction (:01c9-0201):
//     part A: base + 0 (:01d1 — H1 body, NO table offset)
//     part B: base + 6 (:01f0 `add ax,6` — H4[3] SHOCKED strip, 96×57,
//             also covers the right shoulder next to the head)
// Engine mapping: base+0..2 → H1/H2/H3.MAT[0]; base+3..6 → H4.MAT[0..3].
// Outside the window the EXE draws nothing and the VGA frame persists
// showing the counter-20 pair; the full-frame redraw reproduces that as
// {0, 0} (immediate-blit vs deferred-queue intentional divergence).
L6HmatParts l6_select_hmat_parts(const L6BossState& boss) {
    if (boss.hit_reaction_counter > 0) {
        return {0, 3};  // 254f_0078:01d1 (base+0) / :01f0 (base+6)
    }
    constexpr int kAttackTbl[5] = {0, 1, 2, 1, 0};  // DS:0x1fe4
    int pose = 0;
    if (boss.frame_counter >= 16 && boss.frame_counter <= 20) {
        pose = kAttackTbl[boss.frame_counter - 16];
    }
    return {pose, pose};
}

// Victory sequence logic (FUN_254f_02b5 0x0500-0x0641).
// Called once per logic frame from the presentation layer's victory loop.
// Player drops +4/frame until y >= 160; after landing, win_counter counts to 30.
// Defeat sprite cycle_idx advances every 4 ticks throughout.
void update_l6_victory_frame(BossPlayerState& p, L6BossState& boss) {
    // Cycle the defeat sprite index every 4 logic ticks.
    if (++boss.cycle_tick >= 4) {
        boss.cycle_tick = 0;
        boss.cycle_idx = (boss.cycle_idx + 1) & 3;
    }

    constexpr int kFloorY = 160;   // 0xa0 — FUN_254f_02b5 0x0525
    constexpr int kDropDY = 4;     // +4/frame while airborne
    constexpr int kWinMax = 30;    // 0x1e — counter after landing

    if (p.y < kFloorY) {
        p.y += kDropDY;
    } else {
        if (++boss.win_counter >= kWinMax) boss.win_flag = 100;
    }
}

}  // namespace olduvai::systems
