// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// L6 Giant boss logic.                        // FUN_254f_02b5 main
// Frame counter cycles &0x1F (wrap-to-0 jumps to 7); the slam attack
// window is frames 16-20; slams at frames 17/18 are INSTANT-KILL
// (death trigger + knockback stun — no health damage path), guarded
// only on stun + invuln.                       // FUN_254f_0078
// The giant renders as TWO H-atlas parts per animated frame (capstone
// 254f_0078:00a3-00f7): part A body at x=0x50=80 = tbl[c-16]+base,
// part B arm+head strip at x=0xd0=208 = tbl[c-16]+base+3, where tbl =
// DS:0x1fe4 [0,1,2,1,0] (raw EXE bytes at file 0x21974) and base =
// DS:0x9896 (H1..H4.MAT load at base+0..+3, FUN_254f_02b5 0x02f6-0350).
// Club hits land outside the attack window while airborne above y<50
// to the right of x>182: -2 health, counter snap to 19 (254f_0078:01c3)
// and an inline SHOCKED PAIR draw — part A = base+0 (:01d1, H1 body, NO
// table offset), part B = base+6 (:01f0, H4[3] 96×57 shocked arm+head).
// The next tick's inc lands on 20 → the base+0/base+3 window pair (the
// animated tail).  Engine QoL: the shocked pair holds 3 ticks with the
// counter FROZEN at 19 so the tail still plays after the hold (hold=1
// degenerates to the exact EXE tick sequence) — mirrors reference
// boss_l6.py (intentional-divergence-quality-of-life).
// Ground punch zone (99<x<125 on the floor) launches
// the player with jump_peak=70 and -15/frame.  // FUN_254f_0003

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

// `smooth_motion` (enhanced-only) holds each slam pose (frames 16-20) for 2
// logic ticks instead of 1 for clearer reading on modern displays — see
// advance_frame_counter.  Classic / headless callers omit it → EXE-faithful
// 1-tick advance (parity with reference boss_l6.py::_advance_frame_counter).
void update_l6_boss_frame(BossPlayerState& p, L6BossState& boss,
                          const struct BossInputs& in,
                          bool smooth_motion = false);
// Post-render: the shocked-face hold decrement.
void tick_l6_boss_post_render(L6BossState& boss);

// One tick of victory state (called per logic frame from the presentation
// victory loop; FUN_254f_02b5 0x0500-0x0641).  Mutates p.y (drop) and
// boss.{win_counter,cycle_idx,cycle_tick,win_flag}.
// win_flag becomes 100 when win_counter reaches 30.
void update_l6_victory_frame(BossPlayerState& p, L6BossState& boss);

// Two-part H-atlas composite selection (pure; used by the presentation
// render and pinned by tests).  body: 0..2 → H1/H2/H3.MAT[0] at (80,7);
// head: 0..3 → H4.MAT[head] at (208,7).
//   swing window (16..20):  {tbl[c-16], tbl[c-16]}   // 254f_0078:00b4/:00de
//   hit reaction (hold>0):  {0, 3}                   // 254f_0078:01d1/:01f0
//   outside window:         {0, 0}  — full-frame redraw of the persisted
//                           counter-20 pair (EXE draws nothing there).
struct L6HmatParts { int body; int head; };
L6HmatParts l6_select_hmat_parts(const L6BossState& boss);

}  // namespace olduvai::systems
