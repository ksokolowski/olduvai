// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss/boss_diag.hpp"

#include <cstdlib>

#include "presentation/env_num.hpp"          // env_int

namespace olduvai::presentation {

BossHooks BossHooks::from_env() {
    BossHooks h;
    h.pause_shot = std::getenv("OLDUVAI_BOSS_PAUSE_SHOT");
    h.pause_screen = std::getenv("OLDUVAI_BOSS_PAUSE_SCREEN");
    h.pause_midinterp = std::getenv("OLDUVAI_BOSS_PAUSE_MIDINTERP") != nullptr;
    h.force_win = env_int("OLDUVAI_FORCE_WIN", INT_MAX);
    h.force_l4_rideoff = env_int("OLDUVAI_FORCE_L4_RIDEOFF", INT_MAX);
    h.force_smooth = env_int("OLDUVAI_FORCE_SMOOTH", 0) == 1;
    h.real_shot = std::getenv("OLDUVAI_REAL_SHOT") != nullptr;
    return h;
}

void open_pause_shot(BossPause& pause, BossFight& f, SmoothPos& sp,
                     const BossHooks& hooks) {
    if (!pause.open_screen(hooks.pause_screen != nullptr ? hooks.pause_screen
                                                         : "pause_boss"))
        return;
    if (!hooks.pause_midinterp) return;
    sp = {true, static_cast<float>(f.player.x) + 8.0f,
          static_cast<float>(f.player.y) + 8.0f};
    // use_float_pos makes the compose read the other float shadows too (L2
    // rocks, L4 dino): pin them so only the player is offset.
    for (auto& slot : f.l2.slots) slot.fx = static_cast<float>(slot.x);
    f.l4.fx = static_cast<float>(f.l4.boss_x);
    f.l4.fy = static_cast<float>(f.l4.boss_y);
}

bool debug_force_win(BossFight& f, int frame, const BossHooks& hooks) {
    if (f.level == 4 && f.l4.win_flag == 0 && frame >= hooks.force_l4_rideoff)
        f.l4.win_flag = 1;
    return frame >= hooks.force_win;
}

void write_boss_report(const BossFight& f, const BossOps& ops, int frame,
                       const BossAssets& assets, BossView& view,
                       const FrameBuffer& shot, const BugAnnotations& ann) {
    systems::SystemsState snap;
    snap.player.x = f.player.x;
    snap.player.y = f.player.y;
    snap.player.lives = f.player.lives;
    snap.score = f.player.score;
    snap.current_level = f.level;   // boss display == internal
    snap.current_screen = 0;
    BossInfo bi;
    bi.supplied = true;
    bi.health = ops.health();
    bi.phase = ops.phase();
    bi.frame = frame;
    const DisplayInfo di = view.display_info();
    write_bug_report_as_shown(
        {snap, shot, assets.spr, f.level, f.level, ann, di.hd || di.ws_active,
         di, bi},
        view.surface().ren(),
        [&view] { view.arena().present_any(true, /*do_present=*/false); });
}

}  // namespace olduvai::presentation
