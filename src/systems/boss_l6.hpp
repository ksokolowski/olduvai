// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// L6 Giant boss logic.                         // FUN_254f_02b5 main
// The frame counter cycles &0x1F (the wrap jumps to 7); the slam window is
// frames 16-20; slams at 17/18 are instant kills (death + knockback, no
// health path), guarded only on stun + invulnerability.   // FUN_254f_0078
// The giant draws as two H-atlas parts (see l6_select_hmat_parts).  Club hits
// outside the window, airborne above y<50 right of x>182: -2 health, counter
// to 19 (254f_0078:01c3) and the shocked pair; the next tick lands on 20 (the
// animated tail).  Enhanced QoL: the shocked pair holds 3 ticks with the
// counter frozen at 19 (a hold of 1 is the exact EXE sequence), as the
// reference.
// Ground punch zone (99<x<125 on the floor) launches the player with
// jump_peak=70 at -15/frame.  // FUN_254f_0003

#pragma once

#include "systems/boss.hpp"

namespace olduvai::systems {

struct L6BossState {
    int health = kBossHealthStart;     // win when <= 272
    int frame_counter = 15;            // DS:0x9348, starts at 0xF
    int ground_punch_state = 0;        // cycles &3; DS:0x9352
    int win_flag = 0;                  // 0 fight, 1 victory in progress, 100 done
    int win_counter = 0;               // counts 0→30 after player lands; FUN_254f_02b5 0x0500
    int cycle_idx = 0;                 // defeat-sprite cycle index (0-3); advances every 4 frames
    int cycle_tick = 0;                // sub-counter for cycle_idx advance
    int hit_reaction_counter = 0;      // shocked-face hold
    int slam_slow_tick = 0;            // smooth-motion 2-tick slam pose-hold toggle
    bool sfx_hit_pending = false;
};

// `smooth_motion` (enhanced) holds each slam pose (16-20) for 2 ticks; omitted
// = the EXE's 1-tick advance.
void update_l6_boss_frame(BossPlayerState& p, L6BossState& boss,
                          const struct BossInputs& in,
                          bool smooth_motion = false);
// Post-render: the shocked-face hold decrement.
void tick_l6_boss_post_render(L6BossState& boss);

// One victory tick (FUN_254f_02b5 0x0500-0x0641): mutates p.y (drop) and
// boss.{win_counter,cycle_idx,cycle_tick,win_flag}; win_flag = 100 when
// win_counter reaches 30.
void update_l6_victory_frame(BossPlayerState& p, L6BossState& boss);

// Two-part H-atlas selection (pure; pinned by tests).  body 0..2 ->
// H1/H2/H3.MAT[0] at (80,7); head 0..3 -> H4.MAT[head] at (208,7).
//   swing window (16..20):  {tbl[c-16], tbl[c-16]}   // 254f_0078:00b4/:00de
//   hit reaction (hold>0):  {0, 3}                   // 254f_0078:01d1/:01f0
//   outside window:         {0, 0}  (the EXE draws nothing; the VGA frame keeps
//                           the counter-20 pair)
// tbl = DS:0x1fe4 {0,1,2,1,0} (file 0x21974); base = DS:0x9896 (H1..H4.MAT at
// base+0..+3, FUN_254f_02b5 0x02f6-0350).
struct L6HmatParts { int body; int head; };
L6HmatParts l6_select_hmat_parts(const L6BossState& boss);

}  // namespace olduvai::systems
