// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// A boss fight's logic state, and the float render position its smooth
// sub-frames draw at.
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "presentation/input/replay.hpp"          // InputReplay, InputRecorder
#include "presentation/render/boss_render.hpp"    // BossAssets
#include "presentation/render/game_render.hpp"    // RenderTarget
#include "systems/boss.hpp"
#include "systems/boss_l2.hpp"
#include "systems/boss_l4.hpp"
#include "systems/boss_l6.hpp"

namespace olduvai::presentation {

struct GameOptions;

using systems::BossInputs;
using systems::BossPlayerState;
using systems::L2BossState;
using systems::L4BossState;
using systems::L6BossState;

// One fight's logic state.
struct BossFight {
    int level;
    BossPlayerState player;
    L2BossState l2;
    L4BossState l4;
    L6BossState l6;
    // The energy-bar columns the fight has drained, in order.  Recorded in
    // both modes; the classic arena shows them (rebuild_arena_bg).
    std::vector<int> drained_columns;

    int& health() {
        return level == 2 ? l2.health : level == 4 ? l4.health : l6.health;
    }
};

// The player's float render position on a smooth sub-frame; use_float false
// = the integer path.
struct SmoothPos {
    bool use_float = false;
    float fx = 0.0f, fy = 0.0f;
};

void erase_pip_column(BossAssets& a, int health);

// One drained energy-bar column: recorded on the fight, and erased from the
// shown arena when `paint` (the classic HUD).
void drain_pip_column(BossFight& f, BossAssets& a, int column, bool paint);

// The shown arena from the loaded one and the fight's drain: classic erases
// the drained columns; `vector_hud` leaves the picture whole, since the view
// cuts the HUD strip out and draws the bar from health.  A display rebuild
// runs it, so a Style change mid-fight shows the bar the fight has.
void rebuild_arena_bg(BossAssets& a, const BossFight& f, bool vector_hud);

bool load_boss_assets(const std::filesystem::path& dir, int level,
                      BossAssets& a);

// Previous-tick snapshot for the smooth lerp.
void save_prev_positions(BossFight& f);

// What a smooth sub-frame moves: the player, L2's projectile x, the L4 dino's
// x/y (L6's giant is anchored).  Returns the logic values for restore_fight.
struct FightPositions {
    int player_x = 0, player_y = 0;
    std::array<int, 4> slot_x{};
    int boss_x = 0, boss_y = 0;
};

FightPositions interpolate_fight(BossFight& f, float alpha, SmoothPos& sp);

void restore_fight(BossFight& f, const FightPositions& s);

// Per-boss dispatch, bound once.  The remaining `level ==` sites (victory
// cinematics, pip erase, smooth-motion save/restore) have genuinely different
// bodies, each with its own EXE citation.
struct BossOps {
    std::function<void(RenderTarget&)> render_frame;        // pause path
    std::function<void(RenderTarget&)> render_sprites;      // pause, wide
    std::function<void(RenderTarget&)> render_fight_frame;  // fight path
    std::function<void(const BossInputs&)> update_frame;
    std::function<bool()> take_sfx_hit;
    std::function<void(RenderTarget&)> render_victory_sprites;
    // Read-only, for the trace line and the F5 report.
    std::function<int()> health;
    std::function<std::string()> phase;
};

BossOps make_boss_ops(BossFight& f, const BossAssets& a,
                      const int& l2_last_flash, bool smooth);

// `ops` with `effect` drawn after every boss draw path (the fight, the pause
// and the wide sprites): the fly-in balloons.
BossOps draw_after(BossOps ops,
                   const std::function<void(RenderTarget&)>& effect);

// Smooth motion in a fight: in live play only (--play-frames and --play-shot
// want one frame per tick), unless `force` (OLDUVAI_FORCE_SMOOTH, the only
// way a gate reaches the L6 slam pose-hold).  Replay folds per tick, so
// replay + smooth stays deterministic.
bool boss_smooth_motion(const GameOptions& opts, bool force);

// This tick's inputs.  Replay: frame N uses keys folded up to N and frame 0
// is input-free (unlike the surface's at(frame + 1): the 44-frame fly-in pins
// boss alignment at 0 <-> 0).
BossInputs read_boss_inputs(const InputReplay& replay, int frame);

// Recorded at the frame the boss reader resolves (at(frame)); frame 0 skipped.
void record_boss_inputs(InputRecorder& rec, int frame, const BossInputs& in);

// Post-render ticks and HUD pip erasure, once per logic tick after the render
// (draw-then-increment: jaw/arm timers must not fire per sub-frame).  The
// drain is recorded in both modes and erased from the arena only in classic;
// enhanced draws a vector bar.  Returns "won".
bool boss_post_render(BossFight& f, BossAssets& a, int l4_health_before,
                      int l6_health_before, bool erase_pips);

}  // namespace olduvai::presentation
