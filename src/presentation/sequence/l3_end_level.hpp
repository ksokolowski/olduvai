// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// L3 screen 17->18 trunk-descent end-level sequence: FUN_2276_0282 (Phase 1)
// and FUN_2276_03d9 (Phase 2), called from Level3_Main 0x0864-0x0879.
// Trigger: screen 17->18 with food > 0x2C (44), screen clear, player y ==
// 0x44 (68).
// Phase 1: screen 17's first 16 tile records slide down 4 px per iter, 44
// iters x 4 BIOS ticks; player locked; no smoke.  Before bind_screen(18).
// Phase 2: the same 16 records descend from above into screen 18, y_offset
// -80 -> 0 over 21 iters x 4 ticks; smoke puffs on iters 0..19 from the
// pre-rolled jitter (roll_l3_descent_smoke_jitter, FUN_2276_03d9
// 0x0554/0x0586).  After bind_screen(18).
// Afterwards the caller stamps the 16 records as a collision/draw overlay on
// screen 18.

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "formats/mat.hpp"
#include "formats/pc1.hpp"
#include "prepare/exe_tables.hpp"
#include "presentation/render/level_surface.hpp"
#include "systems/player.hpp"

struct SDL_Renderer;
struct SDL_Window;

namespace olduvai::enhance {
class HdText;
}

namespace olduvai::presentation {

struct FrameBuffer;
struct LevelRenderAssets;
class WidescreenPresenter;
class TextOverlay;
struct Loaded;
struct GameOptions;

// Descent tile remap (FUN_2276_03d9:0x0496-0x04b5 +
// FUN_2276_0282:0x032a-0x0347): 29->30, 28->29, 19->31, 4->28, each test
// independent.  Unlike resolve_sprite_idx, which maps 28 -> -1.
int descent_resolve_sprite_idx(int idx);


// The inputs every descent phase takes.  References: each phase is a blocking
// call from run_l3_trunk_descent_sequence, whose locals outlive it.
// tile_sprites indices: 0..27 ELEML3, 28..32 ELEML3B, 33 GROT3 body, 34 cap.
struct L3DescentPhase {
    systems::SystemsState& state;
    const std::vector<formats::Sprite>& tile_sprites;   // L3 tiles + GROT3
    const std::vector<formats::Sprite>& entity_sprites; // L3SPR.MAT
    const std::vector<formats::Rgb>& palette;
    const prepare::LevelTiles& tile_data;
    const std::vector<formats::Sprite>& grot3;
    FrameBuffer& fb;
    bool enhanced;
    // Continue the trunk columns into rows 0-8 (tile_patterns) to match the
    // steady view (enhanced widescreen HUD band); false keeps the EXE black
    // strip.
    bool extend_band;
    const std::function<bool(const FrameBuffer&)>& present;
};

// Phase 1 (FUN_2276_0282): 44 iters, y_offset 0 -> +172, until the records
// leave the screen.  The player is drawn on every iter.  Returns false on ESC.
bool run_l3_screen17_descent(const L3DescentPhase& p);

// Phase 2 (FUN_2276_03d9): 21 iters, y_offset -80 -> 0.  Consumes
// state.l3_descent_smoke_jitter.  Returns false on ESC.
bool run_l3_trunk_descent(const L3DescentPhase& p);

// Enhanced descent camera pan, not in the EXE (off: the hard background swap
// stands).  One vertical pan from Phase 1 to Phase 2: screen 17 recedes up,
// screen 18 enters from below, with the platform and player glued to screen 18
// at Phase 2's start offset, so Phase 2 resumes from the same frame.
// `present` shows each native frame with the HUD.  Returns false on ESC /
// window close; Phase 2 then handles the abort.
bool run_l3_descent_pan(const L3DescentPhase& p);

// The overlay stamped on screen 18 after Phase 2: screen 17's first 16 tile
// placements.  The caller stamps collision and adds them to render.tiles.
std::vector<prepare::TilePlacement> l3_descent_overlay_tiles(
    const prepare::LevelTiles& tile_data);

// Live context for run_l3_trunk_descent_sequence (margins, present callbacks,
// Phase 1 / pan / Phase 2, overlay stamping).  Pointers to the run-loop
// locals, plus scalars fixed for the cinematic.  Sets `*running` false on ESC
// or window close and arms `*l3_smoke_tail`; the caller then sets
// transition kind kNone.
struct DescentCtx {
    // logical_w/logical_h below are snapshots taken at descent entry, not the
    // live surface size.
    LevelSurface* surface = nullptr;
    WidescreenPresenter* wsp = nullptr;
    Loaded* g = nullptr;              // the game bundle (state/render/tiles/dur)
    const GameOptions* opts = nullptr;
    bool* running = nullptr;          // set false on ESC / window-close mid-run
    int* l3_smoke_tail = nullptr;     // armed after Phase 2 (descent-pan gated)
    std::uint32_t frame_ms = 0;
    int prev_screen = 0;              // screen 17 (transition already advanced to 18)
    int logical_w = 0;
    int logical_h = 0;
    int l3_smoke_tail_ticks = 0;      // = kL3SmokeTailTicks (the arm value)
    // The pillarbox present, for the classic / non-widescreen descent.
    std::function<void(FrameBuffer&, bool, bool)> upload_and_show;
};

void run_l3_trunk_descent_sequence(const DescentCtx& c);

}  // namespace olduvai::presentation
