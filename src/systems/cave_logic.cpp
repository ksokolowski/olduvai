// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/cave_logic.hpp"

#include "core/constants.hpp"
#include "core/game_tables.hpp"

namespace olduvai::systems {

void enter_cave(SystemsState& state, int cave_index) {
    if (cave_index == core::kCaveTransitionMarker) {
        // L7-style screen-transition entrances (L1 uses 9→10 and 18→end).
        if (state.current_screen == 9) {
            state.current_screen = 10;
            state.player.x = 10;
            state.player.y = 131;
            // L7-style screen-9 cave ENTRY — EXE FUN_25b2_020b 0x0837 clears
            // bp-6 = 0, routing the next frame through Sprite_DrawDispatch
            // mode=2 (fade-out → blit → fade-in).  This entry short-circuits
            // the cave_warp_freeze 0xFA1>>2 path (descent block calls
            // enter_cave directly), so the fade signal must be raised here.
            state.player.cave_warp_pending = true;
        } else if (state.current_screen == 18) {
            state.current_screen = 19;
            state.level_complete = true;
        }
        state.screen_change = true;
        return;
    }
    state.cave_return_screen = state.current_screen;
    state.cave_return_x = state.player.x;
    state.cave_return_y = state.player.y;
    state.cave_flag = 1;
    state.cave_index = cave_index;
    state.current_screen = 100 + cave_index;
    if (state.current_level == 3) {
        state.player.x = 86;
        state.player.y = 159;
    } else {
        state.player.x = 8;
        state.player.y = kCaveSpawnY;
    }
    state.player.gravity_flag = 0;
    state.player.climbing = 0;
    state.player.club_flag = 0;
    state.player.facing_left = 0;
    state.player.cave_warp_freeze = 0;
    state.player.ghost_rise = 0;
    state.player.death_counter = 0;
    state.screen_change = true;
}

void exit_cave(SystemsState& state) {
    state.cave_flag = 0;
    state.cave_index = -1;
    state.current_screen = state.cave_return_screen;
    state.player.x = state.cave_return_x;
    state.player.y = state.cave_return_y;
    state.player.gravity_flag = 0;
    // Cave-EMERGE animation (owner-ruled divergence) — see
    // SystemsState::cave_emerge_frames.  Enhanced v2 pacing: 9 ticks =
    // 3 stages × 3-tick holds (1/3 → 2/3 → full brightness of
    // PLAYER_TURN), player frozen for the duration (frame_runner gate).
    // Classic: 2 lit ticks, draw-only, no freeze (EXE gameplay timing).
    state.cave_emerge_frames = state.enhanced_active
                                   ? kCaveEmergeTicksEnhanced
                                   : kCaveEmergeTicksClassic;
    state.screen_change = true;
}

void setup_cave_collision(SystemsState& state) {
    state.collision.clear();
    const int idx = state.cave_index;
    if (idx < 0 || idx >= static_cast<int>(core::game_tables().cave_sizes.size())) return;
    const int width = core::game_tables().cave_sizes[static_cast<std::size_t>(idx)];
    const int limit = std::min(width + 24, core::kGameW);
    for (int x = 0; x < limit; ++x) {
        state.collision.set_bit(x, core::kCaveFloorY);
    }
}

void check_cave_exit(SystemsState& state) {
    if (state.cave_flag == 0) return;
    const int idx = state.cave_index;
    if (idx < 0 || idx >= static_cast<int>(core::game_tables().cave_sizes.size())) return;
    const int cave_exit_x =
        (state.current_level == 3)
            ? 183   // hardcoded right edge in the L3 cave init
            : core::game_tables().cave_sizes[static_cast<std::size_t>(idx)] - 8;
    if (state.player.x > cave_exit_x) state.player.x = cave_exit_x;
    if (state.current_level != 3) {
        if (state.player.x < 6) exit_cave(state);
    } else {
        if (state.player.x < 86 && state.player.y > 150) exit_cave(state);
        else if (state.player.x < 85) state.player.x = 85;
    }
}

bool tick_cave_descent(SystemsState& state) {
    const int frame_idx = state.cave_entrance_mask & 3;
    if (frame_idx == 0) return false;
    PlayerState& p = state.player;
    // EXE presentation (byte-walked 2026-07-05, Finding
    // cave_enter_exit_presentation_model.md): the descent shows exactly TWO
    // sprites on the surface — 44 (lit, on the arm frame) then 45 (dim) —
    // and only then does the transition fire.  FUN_27f7_1b51 0x1b6e-0x1b86
    // draws (mask&3)+0x2c at player_x+4 and increments at 0x1b9c; the level
    // main checks (mask&3)==3 AFTER that frame is presented (L1 check at
    // 21f3:07b7-07c5, present at 21f3:0534-0551) and consumes mask>>2 →
    // Cave_Enter at 21f3:07db-07f6.  Sprite 46 (full silhouette) is
    // unreachable in this flow — (mask&3) never survives a frame at 3.
    // The old port code entered the cave on the sprite-45 tick, so 45 was
    // never presented on the surface and leaked into the first cave frames.
    if (frame_idx == 3) {
        // THIRD descent frame (46, full silhouette) — intentional
        // divergence, owner ruling 2026-07-05: the EXE's own art + the
        // (mask&3)+0x2c indexing clearly intend a 3-frame descent, but the
        // post-present ==3 check makes 46 unreachable (a DOS sequencing
        // oversight).  Restored: 46 gets one tick before the transition,
        // so cave entry lands ONE 55 ms tick later than the EXE — the one
        // deliberate gameplay-timing divergence in this path.
        if (!state.cave_descent_third_shown) {
            state.cave_descent_third_shown = true;
            p.sprite = kSprCaveDescent1 + 2;   // 46
            p.dx = 4;
            return true;
        }
        state.cave_descent_third_shown = false;
        const int cave_idx = state.cave_entrance_mask >> 2;
        state.cave_entrance_mask = 0;
        // First cave frame draws the player standing at the spawn point
        // (Cave_Enter's inner loop runs FUN_27f7_1b51's normal branch).
        p.sprite = kSprPlayerStand;
        p.dx = 0;
        enter_cave(state, cave_idx);
        return true;
    }
    p.sprite = kSprCaveDescent1 + (frame_idx - 1);
    p.dx = 4;   // sprite-only draw offset (FUN_27f7_1b51 0x1b7f: add ax, 4)
    ++state.cave_entrance_mask;   // FUN_27f7_1b51 0x1b9c
    return true;
}

}  // namespace olduvai::systems
