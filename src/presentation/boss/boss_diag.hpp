// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// A boss fight's test hooks (docs/internal/DEBUG_HOOKS.md), read once at
// fight entry, and its F5 report.
#pragma once

#include <climits>
#include <string>

#include "presentation/boss/boss_fight.hpp"
#include "presentation/boss/boss_pause.hpp"
#include "presentation/boss/boss_view.hpp"
#include "presentation/diag/bug_capture.hpp"       // BugAnnotations
#include "presentation/render/boss_arena.hpp"
#include "presentation/render/level_surface.hpp"

namespace olduvai::presentation {

// Unset or malformed = off / never.
struct BossHooks {
    const char* pause_shot = nullptr;     // OLDUVAI_BOSS_PAUSE_SHOT=<path>
    const char* pause_screen = nullptr;   // OLDUVAI_BOSS_PAUSE_SCREEN=<id>
    bool pause_midinterp = false;         // OLDUVAI_BOSS_PAUSE_MIDINTERP
    int force_win = INT_MAX;              // OLDUVAI_FORCE_WIN=<frame>
    int force_l4_rideoff = INT_MAX;       // OLDUVAI_FORCE_L4_RIDEOFF=<frame>
    bool force_smooth = false;            // OLDUVAI_FORCE_SMOOTH=1
    bool real_shot = false;               // OLDUVAI_REAL_SHOT

    static BossHooks from_env();
};

// The pause shot: open the pause on `hooks.pause_screen` (default
// "pause_boss"; an unknown id opens nothing).  pause_midinterp freezes
// mid-interpolation: the player's float position 8 px off its logic
// position, enough to see past the menu slab.
void open_pause_shot(BossPause& pause, BossFight& f, SmoothPos& sp,
                     const BossHooks& hooks);

// force_win wins at that frame.  force_l4_rideoff seeds the L4 ride-off
// (win_flag 1 -> 2 -> 3 -> 100), which force_win cannot: it leaves win_flag 0
// and the loop ends before the ride-off draws.
bool debug_force_win(BossFight& f, int frame, const BossHooks& hooks);

// The F5 report of a fight.  An arena has no SystemsState: the shared rows
// are synthesised and the fight goes in BossInfo.  When the present path
// transforms the frame (HD or widescreen), the frame as shown too.
void write_boss_report(const BossFight& f, const BossOps& ops, int frame,
                       const BossAssets& assets, BossView& view,
                       const FrameBuffer& shot, const BugAnnotations& ann);

}  // namespace olduvai::presentation
