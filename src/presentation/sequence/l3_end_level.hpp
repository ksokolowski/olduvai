// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// L3 (Dark Woods) screen-17→18 trunk-descent end-level sequence.
//
// Port of FUN_2276_0282 (Phase 1, 375 B) and FUN_2276_03d9 (Phase 2, 691 B),
// called from Level3_Main 0x0864–0x0879.
//
// Trigger: screen 17→18 with food > 0x2C (44), screen clear,
//          player y == 0x44 (68).
//
// Phase 1 (run_l3_screen17_descent): screen 17's first 16 tile records slide
//   DOWN by +4 px per iter over 44 iters x 4 BIOS ticks each.  Player locked.
//   No smoke.  Runs BEFORE bind_screen(18).
//
// Phase 2 (run_l3_trunk_descent): same 16 records descend FROM ABOVE into
//   screen 18, y_offset -80→0 over 21 iters x 4 ticks.  Smoke A/B/C puffs
//   on iters 0..19 using pre-rolled jitter pairs from state.l3_descent_smoke_jitter
//   (rolled logic-side by systems::roll_l3_descent_smoke_jitter before this
//   call, FUN_2276_03d9 0x0554/0x0586).  Runs AFTER bind_screen(18).
//
// After Phase 2 the caller stamps the 16 records as a collision/draw overlay
// for subsequent screen-18 frames (l3_descent_overlay in the reference).
//
// Finding: l3 screen-18 trunk-descent (internal research notes)
// EXE evidence: FUN_2276_0282 (Phase 1) + FUN_2276_03d9 (Phase 2) walks

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

// Tile-sprite remap for the descent path (0-based, per FUN_2276_03d9:0x0496-0x04b5
// + FUN_2276_0282:0x032a-0x0347).
// CRITICAL: this is the descent-specific chain — differs from resolve_sprite_idx
// in game_app.cpp (which maps 28→-1 for the main loop; the descent maps 28→29).
// 29→30, 28→29, 19→31, 4→28.  No cascading: each test is independent.
int descent_resolve_sprite_idx(int idx);

// Phase 1 — FUN_2276_0282: 44 iterations, y_offset 0→+172.
// Slides screen 17's first 16 tile records DOWN until they disappear.
// Player drawn every iter (no last-iter skip — that's Phase 2 only).
// Returns false if the user pressed ESC (sets game_over externally).
// tile_sprites = combined L3 surface tiles (ELEML3 + ELEML3B) + GROT3 sprites.
// Indices: 0..27=ELEML3, 28..32=ELEML3B, 33=GROT3[0] body, 34=GROT3[1] cap.
// (Matches bind_screen L3 surface path in game_app.cpp.)

// The inputs all three descent phases take, which were the SAME ten parameters
// written out six times — three declarations here and three definitions in the
// .cpp — with the `extend_band` explanation copied alongside each.  Every phase
// is a slice of one cinematic over one set of assets, so there was never a
// second argument list to keep in step, only three more places to forget.
//
// clang-tidy was already saying so: all three tripped
// readability-function-size at "10 parameters (threshold 8)".
//
// Holds references, like the call sites did.  Each phase is a blocking call
// made from run_l3_trunk_descent_sequence, whose locals outlive it.
struct L3DescentPhase {
    systems::SystemsState& state;
    const std::vector<formats::Sprite>& tile_sprites;   // L3 tiles + GROT3
    const std::vector<formats::Sprite>& entity_sprites; // L3SPR.MAT
    const std::vector<formats::Rgb>& palette;
    const prepare::LevelTiles& tile_data;
    const std::vector<formats::Sprite>& grot3;
    FrameBuffer& fb;
    bool enhanced;
    // Enhanced widescreen HUD band: continue trunk columns into rows 0-8 of the
    // descent frames (tile_patterns) so they match the steady view; false keeps
    // the EXE black strip (classic / no vector HUD).
    bool extend_band;
    const std::function<bool(const FrameBuffer&)>& present;
};

bool run_l3_screen17_descent(const L3DescentPhase& p);

// Phase 2 — FUN_2276_03d9: 21 iterations, y_offset -80→0.
// Brings screen 17's first 16 tiles in from above into screen 18.
// On return, fills state.l3_descent_smoke_jitter is consumed;
// the caller stamps the overlay into state for subsequent frames.
// Returns false if the user pressed ESC.
bool run_l3_trunk_descent(const L3DescentPhase& p);

// Enhanced #11 — level-end descent camera-follow pan (opt-in, NOT EXE-faithful).
//
// Bridges Phase 1 (platform slid OFF the bottom of screen 17) and Phase 2
// (platform slid IN from the top of screen 18) with a single continuous
// vertical camera pan: screen 17's backdrop recedes UP while screen 18's
// backdrop enters from BELOW.  The descending platform + player are glued onto
// the incoming screen-18 surface at Phase 2's start offset (kS18YOffsetInit),
// so they scroll into view glued to the trunk and Phase 2 resumes from the
// identical frame (no late pop-in, no seam jump).
//
// Port of run_l3_descent_pan (from the reference implementation).  Gated on the
// descent-pan enhance flag; when OFF this function is NOT called (game_app.cpp)
// and the EXE-faithful hard background swap stands byte-identical.
//
// `present` composites + shows each native 320x200 pan frame WITH the enhanced
// HUD (the caller passes a HUD-drawing present so the HUD stays visible across
// the pan — reference _present_frame draws the HUD on every pan frame).
//
// Returns false if the user pressed ESC / closed the window (sets game_over);
// Phase 2 then handles the abort.
bool run_l3_descent_pan(const L3DescentPhase& p);

// Descent-overlay tile records stamped on screen 18 after Phase 2 completes.
// Returns the first 16 tile placements from screen 17 (the reference's
// l3_descent_overlay list).  Caller stamps collision + adds to render.tiles.
std::vector<prepare::TilePlacement> l3_descent_overlay_tiles(
    const prepare::LevelTiles& tile_data);

// Live context for run_l3_trunk_descent_sequence — the whole screen-17→18
// trunk-descent orchestration (margins + present callbacks + Phase 1 / pan /
// Phase 2 + overlay stamping), lifted verbatim out of run_platform_level's
// post-frame transition block (game_app.cpp).  Holds POINTERS to the run-loop
// locals (so the body sees live values, like the old [&] captures) plus value
// scalars stable across the blocking cinematic.  The sequence NEVER returns a
// status — it drives `*running` false on ESC/window-close mid-descent and arms
// `*l3_smoke_tail` (a cross-cutting counter the render path decrements), exactly
// as the inline block did; the caller sets transition_kind = 0 afterwards.
struct DescentCtx {
    // The level's presentation surface: renderer, window, vector font, overlay
    // and the HD settings — seven separate members here until §3.7 cluster 1,
    // the same list FramePresenter and WidescreenShellCtx each carried.
    //
    // logical_w/logical_h below are NOT part of it, deliberately: they are
    // SNAPSHOTS of the logical size at descent entry, and the descent is a
    // blocking cinematic during which a resize could move the live value.
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
    // The per-frame pillarbox/present path (upload_and_show_fn view): the
    // classic / non-widescreen descent fallback hands each native frame here.
    std::function<void(FrameBuffer&, bool, bool)> upload_and_show;
};

void run_l3_trunk_descent_sequence(const DescentCtx& c);

}  // namespace olduvai::presentation
