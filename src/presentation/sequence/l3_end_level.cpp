// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// L3 (Dark Woods) trunk-descent end-level sequence.
// Port of FUN_2276_0282 (Phase 1, 375 B) and FUN_2276_03d9 (Phase 2, 691 B).
//
// EXE capstone sources:
//   FUN_2276_0282 walk (Phase 1)
//   FUN_2276_03d9 walk (Phase 2)
// Finding: l3 screen-18 trunk-descent (internal research notes)

#include "presentation/sequence/l3_end_level.hpp"

#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

#include "enhance/enhanced_hud.hpp"           // compute/draw_enhanced_hud_*
#include "enhance/hd_text.hpp"                // enhance::HdText
#include "enhance/upscale.hpp"                // upscale_rgba
#include "presentation/diag/bug_capture.hpp"       // bug_report_root
#include "presentation/game_app.hpp"          // GameOptions
#include "presentation/render/game_render.hpp"
#include "presentation/image_out.hpp"         // save_rgba_image/save_surface_image
#include "presentation/level/level_setup.hpp"       // Loaded, bind_screen
#include "presentation/render/tile_patterns.hpp"
#include "presentation/render/text_overlay.hpp"      // TextOverlay
#include "presentation/render/widescreen_presenter.hpp"  // WidescreenPresenter
#include "presentation/window_util.hpp"       // poll_screen_events
#include "systems/transitions.hpp"            // roll_l3_descent_smoke_jitter, clear_per_screen_state

namespace olduvai::presentation {

// ── EXE-verified constants (Phase 1 — FUN_2276_0282) ─────────────────────────

// FUN_2276_0282:0x03ca — iteration bound 0x2c → 44 iters (0..43).
constexpr int kS17Iters = 44;
// y_offset starts at 0 (0x028a), increments by 4 each iter (0x03c3).
constexpr int kS17YStep = 4;
// 16 tile records per iter (same as Phase 2).
constexpr int kDescendTiles = 16;
// 4 BIOS ticks per iter (same call pattern as Phase 2).
constexpr int kTicksPerIter = 4;

// ── EXE-verified constants (Phase 2 — FUN_2276_03d9) ─────────────────────────

// FUN_2276_03d9:0x0668 — iteration bound 0x15 → 21 iters (0..20).
constexpr int kS18Iters = 21;
// y_offset starts at -80 (init value 0xffb0 at 0x03e1).
// EXE increments by 4 per outer iter (0x0661); the port advances 1 px per
// logical frame instead (matching Phase 1 + the reference),
// so kS18YStep is documented here but not used at runtime.
constexpr int kS18YOffsetInit = -80;
// constexpr int kS18YStep = 4;  // EXE value; port uses per-frame +1 instead

// ── Tile remap (0-based, descent-path specific) ───────────────────────────────
// FUN_2276_03d9:0x0496-0x04b5 + FUN_2276_0282:0x032a-0x0347.
// CRITICAL: 28→29 (NO skip — differs from the main-loop resolve_sprite_idx
// which maps 28→-1).  The EXE descent remaps unconditionally.
int descent_resolve_sprite_idx(int idx) {
    switch (idx) {
        case 29: return 30;
        case 28: return 29;   // descent-path only: no skip, goes to 29
        case 19: return 31;
        case  4: return 28;
        default: return idx;
    }
}

// ── Screen 17 tile index (DESCENT_TILES_SCREEN in reference) ─────────────────
// FUN_2276_03d9:0x0415 overwrites the screen arg to 0x11=17 so Phase 2 also
// draws screen-17 content.  Both phases use screen 17's first 16 records.
constexpr int kScreen17 = 17;
constexpr int kScreen18 = 18;

// ── GROT3 trunk column geometry (reference only) ─────────────────────────────
// x=98; body (GROT3[0]) at y 167/128/89/50; cap (GROT3[1]) at y=23.
// The descent backdrops (screens 17/18, both != 10/11) net-hide the trunk
// (EXE draw->black-fill->pine order); trunk geometry is documented here for
// reference but not drawn in build_l3_bg_base.  See game_app.cpp bind_screen.
// constexpr int kTrunkX   = 98;
// constexpr int kTrunkCapY = 23;
// constexpr int kTrunkBodyYs[4] = {167, 128, 89, 50};

// ── Smoke constants (Phase 2, FUN_2276_03d9:0x053e-0x05c5) ───────────────────
// Smoke A: L3SPR[85+(iter&1)], x=49, y=175+jitter_a
constexpr int kSmokeASprBase = 85;
constexpr int kSmokeAX = 49;
constexpr int kSmokeAYBase = 175;
// Smoke B: L3SPR[86+(iter&1)], x=65, y=170+jitter_b
constexpr int kSmokeBSprBase = 86;
constexpr int kSmokeBX = 65;
constexpr int kSmokeBYBase = 170;
// Smoke C: L3SPR[85+(iter&1)], x=79, y=178 (fixed)
constexpr int kSmokeCSprBase = 85;
constexpr int kSmokeCX = 79;
constexpr int kSmokeCY = 178;

// ── Decoration tiles (Phase 2, FUN_2276_03d9:0x05c8-0x0638) ──────────────────
// 4 ELEML3 tiles at y=185 every iter except last.
constexpr int kDecY = 185;
struct DecTile { int x; int sprite_idx; };   // sprite_idx in eleml3
constexpr DecTile kDecorations[4] = {{16,0},{32,1},{80,1},{128,1}};

// Pine silhouette: ELEML3B[3] (index 31 in the combined surface_tiles).
// Index 31 = 28 (ELEML3 sprites) + 3 (ELEML3B[3]).
constexpr int kPineSprIdx = 31;
// GROT3 body = index surface_tiles.size() (33 when 28+5 ELEML3+ELEML3B).
// Cap = surface_tiles.size()+1.  Passed as kGrot3BodyIdx computed at runtime.

// ── Helper: build a static background frame ───────────────────────────────────
// Renders black fill + pine silhouettes.  Descent screens 17 and 18 are both
// != screens 10/11, so the net effect of the EXE's draw->black-fill->pine
// order leaves the trunk invisible and shows only the dark
// backdrop + pine silhouettes.  The trunk is NOT drawn here.
// tile_sprites must already have GROT3 appended (indices 33/34).
static void build_l3_bg_base(FrameBuffer& bg,
                              const std::vector<formats::Sprite>& tile_sprites,
                              const std::vector<formats::Rgb>& palette,
                              const std::vector<formats::Sprite>& /*grot3*/) {
    // Black fill.
    std::fill(bg.px.begin(), bg.px.end(), 0);
    for (std::size_t i = 3; i < bg.px.size(); i += 4) bg.px[i] = 255;

    // Pine silhouette at (0,9) and (160,9) — ELEML3B[3] = index 31.
    if (kPineSprIdx < static_cast<int>(tile_sprites.size())) {
        const auto& pine = tile_sprites[static_cast<std::size_t>(kPineSprIdx)];
        blit_sprite(bg, pine, palette, 0, 9);
        blit_sprite(bg, pine, palette, 160, 9);
    }
}

// Blit a screen's tile records [begin_idx..) onto fb (alias-resolved).  Under
// `extend_band` the trunk columns are first continued up through the HUD-strip
// band (tile_patterns — same rule as the steady widescreen view), so the
// descent frames carry the same top band as the screens they bridge instead of
// the EXE black strip; classic (extend_band=false) keeps the strip black.
static void blit_screen_tiles(FrameBuffer& fb,
                              const std::vector<prepare::TilePlacement>& src,
                              int begin_idx,
                              const std::vector<formats::Sprite>& tile_sprites,
                              const std::vector<formats::Rgb>& palette,
                              bool extend_band) {
    std::vector<LevelRenderAssets::TileDraw> list;
    list.reserve(src.size());
    for (int i = begin_idx; i < static_cast<int>(src.size()); ++i) {
        const auto& tp = src[static_cast<std::size_t>(i)];
        const int idx = descent_resolve_sprite_idx(tp.sprite_idx);
        if (idx >= 0 && idx < static_cast<int>(tile_sprites.size()))
            list.push_back({idx, tp.x, tp.y});
    }
    if (extend_band)
        tile_patterns::extend_columns_to_top(list, tile_sprites);
    for (const auto& t : list)
        blit_sprite(fb, tile_sprites[static_cast<std::size_t>(t.sprite_idx)],
                    palette, t.x, t.y);
}

// ── Enhanced dead-tail trim (reference _phase1_offscreen_offset, py:481-498) ──
// Smallest Phase-1 y_offset at which the player + every descending screen-17
// record has slid fully below the visible area (its top edge reaches GAME_H).
// The EXE Phase 1 is a FIXED 44-iter slide to y_offset 172, so the platform
// clears the screen well before the loop ends — the original shows ~2.5 s of a
// static empty screen.  Enhanced descent-pan ends Phase 1 at this offset so the
// camera-pan follows with no dead-time.  Default mode keeps the full slide.
static constexpr int kGameH = 200;   // native game-surface height (GAME_H)

static int phase1_offscreen_offset(int locked_y,
                                   const std::vector<prepare::TilePlacement>& descent,
                                   int descent_count) {
    int min_y = locked_y;
    for (int i = 0; i < descent_count &&
                    i < static_cast<int>(descent.size()); ++i) {
        if (descent[static_cast<std::size_t>(i)].y < min_y)
            min_y = descent[static_cast<std::size_t>(i)].y;
    }
    return kGameH - min_y;
}

// Render a tile placement from the descent record set with the remap chain.
static void blit_descent_tile(FrameBuffer& fb,
                               const prepare::TilePlacement& tp,
                               const std::vector<formats::Sprite>& tile_sprites,
                               const std::vector<formats::Rgb>& palette,
                               int y_offset) {
    const int idx = descent_resolve_sprite_idx(tp.sprite_idx);
    if (idx < 0 || idx >= static_cast<int>(tile_sprites.size())) return;
    blit_sprite(fb, tile_sprites[static_cast<std::size_t>(idx)], palette,
                tp.x, tp.y + y_offset);
}

// ── Phase 1 ───────────────────────────────────────────────────────────────────

bool run_l3_screen17_descent(const L3DescentPhase& p)
{
    systems::SystemsState& state = p.state;
    const std::vector<formats::Sprite>& tile_sprites = p.tile_sprites;
    const std::vector<formats::Sprite>& entity_sprites = p.entity_sprites;
    const std::vector<formats::Rgb>& palette = p.palette;
    const prepare::LevelTiles& tile_data = p.tile_data;
    const std::vector<formats::Sprite>& grot3 = p.grot3;
    FrameBuffer& fb = p.fb;
    const bool enhanced = p.enhanced;
    const bool extend_band = p.extend_band;
    const std::function<bool(const FrameBuffer&)>& present = p.present;

    // Guard: need screen 17's tile records.
    if (static_cast<int>(tile_data.screens.size()) <= kScreen17) return true;
    const auto& s17tiles = tile_data.screens[static_cast<std::size_t>(kScreen17)].tiles;
    if (static_cast<int>(s17tiles.size()) < kDescendTiles) return true;

    // Descent records: first 16 of screen 17.
    const int descent_count = kDescendTiles;

    // Lock player position at trigger time (player_y == 0x44 at trigger).
    const int locked_x = state.player.x;
    const int locked_y = state.player.y;
    // EXE FUN_2276_0282:0x0390 uses [0x988a] (L3SPR base) → sprite 0.
    constexpr int kLockedSprite = 0;

    // Build static background: screen-17 view minus the first 16 records.
    // (Records [0..15] are the descent platform — drawn per-frame with offset.)
    FrameBuffer bg_static;
    build_l3_bg_base(bg_static, tile_sprites, palette, grot3);
    // Draw screen-17 records AFTER index 15 at their normal positions.
    blit_screen_tiles(bg_static, s17tiles, kDescendTiles, tile_sprites,
                      palette, extend_band);

    // Pacing: 44 iters x 4 ticks = 176 render frames at 18 Hz (≈ 9.7 s).
    // Classic: 1 sub-frame per iter-tick (one compose per 4-tick wait).
    // Enhanced smooth-motion: 3 sub-frames per logical 4-tick step.
    // PHASE 1 DOES NOT INTERPOLATE, so it renders ONE frame per logical frame.
    //
    // `y_offset` below is `frame / substeps` — integer division — so with
    // substeps=3 the three enhanced sub-frames composed a BYTE-IDENTICAL image
    // and each paid a full wide omniscale upscale of the whole (320+2M)x200
    // canvas.  Three times the most expensive operation in the renderer, for
    // pixels already on screen.
    //
    // Headless that is merely wasteful; on a real display it is the reported
    // "descent is very slow" (playtest 2026-07-28, Linux widescreen).  Each
    // sub-frame gets frame_ms/3 = 18 ms, and one wide omniscale upscale does
    // not fit in 18 ms — measured 25 ms worst-frame at widescreen in BACKLOG
    // 3.6c — so every sub-frame overshoots its slice AND misses a vblank, and
    // the error is tripled per logical frame.  One frame per 55 ms fits with
    // room to spare, which is why the slowness ends the moment the cinematic
    // does and the main loop takes over.
    //
    // NOT the same as the earlier "pathetic perf" regression the caller's
    // pacing comment describes: that was sub-frames each paced at the FULL
    // frame_ms (3x the duration).  Here the sub-frames are gone, so one frame
    // at frame_ms is exactly the reference duration.  The camera pan is the
    // one stage that genuinely interpolates and it keeps its sub-frames.
    const int substeps = 1;
    const int total_frames = kS17Iters * kTicksPerIter;   // 176 logical frames
    const int final_offset = (kS17Iters - 1) * kS17YStep;  // 172

    // Enhanced descent-pan: trim the dead tail.  The EXE runs the full
    // fixed-length slide even after the platform clears the screen (~2.5 s of
    // static empty screen).  In descent-pan mode end Phase 1 the moment the
    // last element exits the bottom, so the camera-pan follows immediately.
    // Default mode keeps the full EXE-faithful slide (trim_offset = final_offset,
    // no-op).
    const int trim_offset =
        enhanced
            ? std::min(final_offset,
                       phase1_offscreen_offset(locked_y, s17tiles, descent_count))
            : final_offset;

    for (int frame = 0; frame < total_frames * substeps; ++frame) {
        // Once everything has slid off the bottom there is nothing left to show
        // — stop instead of holding on an empty screen.  Mirrors the reference
        // (only fires when the trim is active).
        if (trim_offset < final_offset &&
            (frame / substeps) >= trim_offset) {
            break;
        }
        // y_offset advances 1 px per logical frame (4 px per iter).
        // Final value = (kS17Iters-1)*kS17YStep = 172.
        // Sub-frame interpolation: frame/substeps gives the fractional
        // logical frame index; clamp to final.
        const int y_offset_raw = frame / substeps;   // logical-frame index
        const int y_offset = std::min(y_offset_raw, final_offset);

        // Drain SDL events (ESC is inert during the descent; window-close aborts).
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { state.game_over = true; return false; }
            // ESC is inert during the descent cinematic (no menu wired here) —
            // it used to silently abort the ENTIRE run to the title.  Only a
            // real window-close (SDL_QUIT, above) stops it.
            (void)0;
        }

        // Compose frame into fb.
        fb = bg_static;

        // 16 descent records with y_offset.
        for (int i = 0; i < descent_count; ++i) {
            blit_descent_tile(fb, s17tiles[static_cast<std::size_t>(i)],
                              tile_sprites, palette, y_offset);
        }

        // Player at (locked_x, locked_y + y_offset) — sprite 0 (standing).
        // EXE draws on every iter including the last (no last-iter skip in Phase 1).
        if (kLockedSprite < static_cast<int>(entity_sprites.size())) {
            blit_sprite(fb, entity_sprites[static_cast<std::size_t>(kLockedSprite)],
                        palette, locked_x, locked_y + y_offset);
        }

        if (!present(fb)) { state.game_over = true; return false; }
    }
    return true;
}

// ── Phase 2 ───────────────────────────────────────────────────────────────────

bool run_l3_trunk_descent(const L3DescentPhase& p)
{
    systems::SystemsState& state = p.state;
    const std::vector<formats::Sprite>& tile_sprites = p.tile_sprites;
    const std::vector<formats::Sprite>& entity_sprites = p.entity_sprites;
    const std::vector<formats::Rgb>& palette = p.palette;
    const prepare::LevelTiles& tile_data = p.tile_data;
    const std::vector<formats::Sprite>& grot3 = p.grot3;
    FrameBuffer& fb = p.fb;
    const bool enhanced = p.enhanced;
    const bool extend_band = p.extend_band;
    const std::function<bool(const FrameBuffer&)>& present = p.present;

    // Guard: need both screen 17 and screen 18 tile data.
    if (static_cast<int>(tile_data.screens.size()) <= kScreen18) return true;
    const auto& s17tiles = tile_data.screens[static_cast<std::size_t>(kScreen17)].tiles;
    const auto& s18tiles = tile_data.screens[static_cast<std::size_t>(kScreen18)].tiles;
    if (static_cast<int>(s17tiles.size()) < kDescendTiles) return true;

    // Lock player position (same as Phase 1 — player_y == 0x44 at trigger).
    const int locked_x = state.player.x;
    const int locked_y = state.player.y;
    constexpr int kLockedSprite = 0;   // EXE FUN_2276_03d9:0x0527 uses L3SPR base

    // Pre-rendered static background: screen 18 tiles at normal positions.
    // FUN_2276_03d9 calls FUN_2276_000d (paint bg) once before the loop (0x03e6).
    FrameBuffer bg_static;
    build_l3_bg_base(bg_static, tile_sprites, palette, grot3);
    // Screen-18 tile placements at normal positions.
    blit_screen_tiles(bg_static, s18tiles, 0, tile_sprites, palette,
                      extend_band);

    // Smoke jitter pairs pre-rolled logic-side (roll_l3_descent_smoke_jitter).
    const auto& jitter = state.l3_descent_smoke_jitter;

    // Pacing: 21 iters x 4 ticks = 84 logical frames at 18 Hz (≈ 4.7 s).
    // ONE frame per logical frame — same reasoning as Phase 1 above, and this
    // phase's own comment already admitted it: "Enhanced (substeps==3):
    // logical_frame == frame/3, HOLDS 3 SUB-FRAMES each px step".  A held frame
    // re-upscaled is a duplicate, not motion.
    //
    // The enhanced dust is a duplicate too, and that is not obvious: its jitter
    // is `hash_jit(outer_iter, slot)` — keyed on the ITER, not the sub-frame —
    // so the puffs sat still for all three as well.  Nothing here varied
    // sub-frame to sub-frame.
    const int substeps = 1;
    const int total_frames = kS18Iters * kTicksPerIter;   // 84

    for (int frame = 0; frame < total_frames * substeps; ++frame) {
        // y_offset: -80 + (logical frame index), clamped to 0.
        // Under smooth-motion each sub-frame advances 1/substeps px.
        // Classic: integer, advancing by kS18YStep each iter (every 4 frames).
        const int outer_iter = (frame / substeps) / kTicksPerIter;

        // Per-frame y_offset interpolation: kS18YOffsetInit → 0 over 84 frames.
        // Advances 1 px per logical frame in both classic and enhanced modes,
        // matching Phase 1 and the reference:
        //   y_offset = min(_Y_OFFSET_INIT + sub / substeps, 0)
        // Classic (substeps==1): logical_frame == frame, steps +1/frame.
        // Enhanced (substeps==3): logical_frame == frame/3, holds 3 sub-frames
        // each px step (same net timing as Phase 1 smooth-motion).
        const int logical_frame = frame / substeps;
        const int y_offset = std::min(kS18YOffsetInit + logical_frame, 0);

        // Drain SDL events.
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { state.game_over = true; return false; }
            // ESC is inert during the descent cinematic (no menu wired here) —
            // it used to silently abort the ENTIRE run to the title.  Only a
            // real window-close (SDL_QUIT, above) stops it.
            (void)0;
        }

        fb = bg_static;

        // 16 descent records from SCREEN 17 with y_offset.
        // EXE FUN_2276_03d9:0x0415 overwrites screen arg to 17.
        for (int i = 0; i < kDescendTiles; ++i) {
            blit_descent_tile(fb, s17tiles[static_cast<std::size_t>(i)],
                              tile_sprites, palette, y_offset);
        }

        // Player — EXE skips on last iter (outer_iter==20), but reference
        // DIVERGES: always draws the player to avoid the ~220 ms invisible flash.
        // We match the reference (documented divergence from EXE).
        if (kLockedSprite < static_cast<int>(entity_sprites.size())) {
            blit_sprite(fb,
                        entity_sprites[static_cast<std::size_t>(kLockedSprite)],
                        palette, locked_x, locked_y + y_offset);
        }

        // Smoke on iters 0..19 only (EXE 0x0516-0x051c skips iter 20).
        if (outer_iter < kS18Iters - 1) {
            const int ji = std::min(outer_iter, static_cast<int>(jitter.size()) - 1);
            const int jit_a = (ji >= 0) ? jitter[static_cast<std::size_t>(ji)].first  : 0;
            const int jit_b = (ji >= 0) ? jitter[static_cast<std::size_t>(ji)].second : 0;

            // Smoke A: L3SPR[85 + (outer_iter & 1)], x=49, y=175+jit_a
            const int smoke_a_idx = kSmokeASprBase + (outer_iter & 1);
            if (smoke_a_idx < static_cast<int>(entity_sprites.size()))
                blit_sprite(fb,
                            entity_sprites[static_cast<std::size_t>(smoke_a_idx)],
                            palette, kSmokeAX, kSmokeAYBase + jit_a);

            // Smoke B: L3SPR[86 + (outer_iter & 1)], x=65, y=170+jit_b
            const int smoke_b_idx = kSmokeBSprBase + (outer_iter & 1);
            if (smoke_b_idx < static_cast<int>(entity_sprites.size()))
                blit_sprite(fb,
                            entity_sprites[static_cast<std::size_t>(smoke_b_idx)],
                            palette, kSmokeBX, kSmokeBYBase + jit_b);

            // Smoke C: L3SPR[85 + (outer_iter & 1)], x=79, y=178 (fixed)
            const int smoke_c_idx = kSmokeCSprBase + (outer_iter & 1);
            if (smoke_c_idx < static_cast<int>(entity_sprites.size()))
                blit_sprite(fb,
                            entity_sprites[static_cast<std::size_t>(smoke_c_idx)],
                            palette, kSmokeCX, kSmokeCY);
        }

        // ── Enhanced descent dust (gated with the enhanced descent package) ──
        // The EXE effect is 3 small puffs that cut out exactly at the landing
        // iter.  Enhanced adds: (a) 3 extra puffs spread across the trunk base
        // while it grinds down, (b) a 6-puff impact burst on the landing iter
        // the EXE leaves dry.  RENDER-ONLY randomness: jitter is an
        // iter/slot-indexed integer hash — the game LCG (and the rng-critical
        // pre-rolled jitter pairs) is never touched, so replays are unaffected.
        if (enhanced) {
            auto hash_jit = [](int i, int k) -> int {
                std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u ^
                                  (static_cast<std::uint32_t>(k) * 0x9E3779B9u);
                return static_cast<int>((h >> 16) & 7u);   // 0..7, EXE-like range
            };
            auto puff = [&](int slot, int x, int y_base) {
                const int idx =
                    kSmokeASprBase + ((outer_iter + slot) & 1);   // 85/86 mix
                if (idx < static_cast<int>(entity_sprites.size()))
                    blit_sprite(
                        fb, entity_sprites[static_cast<std::size_t>(idx)],
                        palette, x, y_base + hash_jit(outer_iter, slot));
            };
            if (outer_iter < kS18Iters - 1) {
                // (a) denser grind dust: widen the EXE's 49..79 band.
                puff(3, 33, 176);
                puff(4, 95, 173);
                puff(5, 111, 177);
            } else {
                // (b) impact burst across the whole base at the slam.
                puff(0, 33, 175);
                puff(1, 49, 172);
                puff(2, 65, 176);
                puff(3, 79, 173);
                puff(4, 95, 175);
                puff(5, 111, 177);
            }
        }

        // 4 decoration tiles at y=185 — every iter except last (EXE 0x05c8).
        // The EXE renders these every outer iter (the last-iter check at
        // 0x0516 only jumps over player+smoke, not decorations — they DO render
        // on iter 20).  The reference does the same.
        // ELEML3[0] = tile_sprites[0], ELEML3[1] = tile_sprites[1] since
        // tile_sprites starts with the 28 ELEML3 sprites.
        if (outer_iter < kS18Iters) {
            for (const auto& d : kDecorations) {
                if (d.sprite_idx < static_cast<int>(tile_sprites.size()))
                    blit_sprite(fb,
                                tile_sprites[static_cast<std::size_t>(d.sprite_idx)],
                                palette, d.x, kDecY);
            }
        }

        if (!present(fb)) { state.game_over = true; return false; }
    }
    return true;
}

// ── Enhanced #11 — descent camera-follow pan ────────────────────────────────
//
// Port of run_l3_descent_pan (from the reference implementation).  Bridges
// Phase 1 → Phase 2 with one continuous vertical pan (screen 17 backdrop UP,
// screen 18 backdrop in from BELOW).  The descending platform + player are glued
// onto the incoming screen-18 surface at Phase 2's start offset (kS18YOffsetInit)
// so they scroll in glued to the trunk, and Phase 2's first frame is identical
// to the pan's last frame (no seam jump, no late pop-in).
//
// NOT EXE-faithful (capstone-confirmed: the EXE hard-swaps the two backdrops —
// invisible since screens 17/18 share the dark-woods art).  Enhanced-mode only.
//
// Composes native 320x200 surfaces and scrolls them with a `blit_shifted`-style
// 'down' pan (same geometry as game_app.cpp kind-3 'D' slide), presenting each
// frame via `present` (which the caller wires to draw the enhanced HUD so the
// HUD stays visible across the pan).
bool run_l3_descent_pan(const L3DescentPhase& p)
{
    systems::SystemsState& state = p.state;
    const std::vector<formats::Sprite>& tile_sprites = p.tile_sprites;
    const std::vector<formats::Sprite>& entity_sprites = p.entity_sprites;
    const std::vector<formats::Rgb>& palette = p.palette;
    const prepare::LevelTiles& tile_data = p.tile_data;
    const std::vector<formats::Sprite>& grot3 = p.grot3;
    FrameBuffer& fb = p.fb;
    const bool enhanced = p.enhanced;
    const bool extend_band = p.extend_band;
    const std::function<bool(const FrameBuffer&)>& present = p.present;

    if (!enhanced) return true;   // pan is descent-pan-only; hard swap stands
    // Guard: need both screen 17 and screen 18 tile data.
    if (static_cast<int>(tile_data.screens.size()) <= kScreen18) return true;
    const auto& s17tiles = tile_data.screens[static_cast<std::size_t>(kScreen17)].tiles;
    const auto& s18tiles = tile_data.screens[static_cast<std::size_t>(kScreen18)].tiles;
    if (static_cast<int>(s17tiles.size()) < kDescendTiles) return true;

    // Build the two static native backdrops.  Screen 17 EXCLUDES its first 16
    // records (the descending platform) — Phase 1 already slid those off the
    // bottom, so re-drawing them here paints a SECOND, static platform/trunk that
    // double-images over the descending one and over the player during the pan.
    // Matches Phase 1's bg_static (loop from kDescendTiles) and the reference
    // _build_screen17_background (screens[17].tiles[16:]).
    FrameBuffer s17{};   // native 320x200
    build_l3_bg_base(s17, tile_sprites, palette, grot3);
    blit_screen_tiles(s17, s17tiles, kDescendTiles, tile_sprites, palette,
                      extend_band);
    FrameBuffer s18{};   // native 320x200
    build_l3_bg_base(s18, tile_sprites, palette, grot3);
    blit_screen_tiles(s18, s18tiles, 0, tile_sprites, palette, extend_band);

    // Glue the descending platform + player onto the INCOMING screen-18 surface
    // at Phase 2's exact start position (y_offset = kS18YOffsetInit = -80) so
    // they scroll into view WITH screen 18 — Phase 2's first frame draws this
    // same content at the same offset on a fresh screen-18 bg, making the pan's
    // last frame and Phase 2's first frame identical (reference py:919-939).
    for (int i = 0; i < kDescendTiles; ++i) {
        blit_descent_tile(s18, s17tiles[static_cast<std::size_t>(i)],
                          tile_sprites, palette, kS18YOffsetInit);
    }
    constexpr int kLockedSprite = 0;   // L3SPR[0] standing (matches Phase 2)
    if (kLockedSprite < static_cast<int>(entity_sprites.size()))
        blit_sprite(s18, entity_sprites[static_cast<std::size_t>(kLockedSprite)],
                    palette, state.player.x,
                    state.player.y + kS18YOffsetInit);

    // 'down' pan, native coords (game_app.cpp kind-3 'D' geometry):
    //   old (screen 17) recedes UP off the top:  ody = -t*H
    //   new (screen 18) enters from the BOTTOM:   ndy = H + ody
    // 12 frames classic, 12*substeps under smooth-motion (wall-clock glide held
    // constant by the caller's frame delay).  Reference n_frames = 12*substeps,
    // py:980-985.
    const int substeps = enhanced ? 3 : 1;
    const int n_frames = 12 * substeps;
    const int H = kGameH;   // 200

    for (int f = 1; f <= n_frames; ++f) {
        const double t = static_cast<double>(f) / static_cast<double>(n_frames);
        const int ody = -static_cast<int>(t * H);
        const int ndy = H + ody;

        // Black-fill the native compose buffer.
        fb = FrameBuffer{};
        std::fill(fb.px.begin(), fb.px.end(), 0);
        for (std::size_t i = 3; i < fb.px.size(); i += 4) fb.px[i] = 255;

        // blit_shifted (vertical only): copy src rows into dst shifted by sdy.
        auto blit_shifted_v = [](FrameBuffer& dst, const FrameBuffer& src,
                                 int sdy) {
            for (int y2 = 0; y2 < kGameH; ++y2) {
                const int sy = y2 - sdy;
                if (sy < 0 || sy >= kGameH) continue;
                std::copy_n(
                    src.px.begin() + static_cast<std::size_t>(sy) * 320 * 4,
                    static_cast<std::size_t>(320) * 4,
                    dst.px.begin() + static_cast<std::size_t>(y2) * 320 * 4);
            }
        };
        blit_shifted_v(fb, s17, ody);
        blit_shifted_v(fb, s18, ndy);

        if (!present(fb)) { state.game_over = true; return false; }
    }
    return true;
}

std::vector<prepare::TilePlacement> l3_descent_overlay_tiles(
    const prepare::LevelTiles& tile_data)
{
    if (static_cast<int>(tile_data.screens.size()) <= kScreen17) return {};
    const auto& tiles = tile_data.screens[static_cast<std::size_t>(kScreen17)].tiles;
    const int n = std::min(kDescendTiles, static_cast<int>(tiles.size()));
    return std::vector<prepare::TilePlacement>(tiles.begin(),
                                               tiles.begin() + n);
}


void run_l3_trunk_descent_sequence(const DescentCtx& c) {
    // Bind the live context (pointers → run_platform_level locals) to the names
    // the block below uses, so the body is a VERBATIM move of the old inline
    // `if (l3_trunk_descent) { ... }` transition branch (game_app.cpp).  The
    // sequence composes each native descent frame into the widescreen margins,
    // runs Phase 1 / the enhanced pan / Phase 2, and stamps the screen-18
    // overlay — touching NO extra LCG/RNG state (the smoke jitter is rolled
    // logic-side), so the cross-engine trace is byte-identical to the classic
    // hard-swap when descent-pan is off.
    WidescreenPresenter& wsp = *c.wsp;
    SDL_Renderer* const ren = c.surface->ren();
    SDL_Window* const win = c.surface->win();
    enhance::HdText& hd_text = c.surface->hd_text();
    TextOverlay& text_overlay = c.surface->overlay();
    Loaded& g = *c.g;
    const GameOptions& opts = *c.opts;
    bool& running = *c.running;
    int& l3_smoke_tail = *c.l3_smoke_tail;
    const bool hd = c.surface->hd();
    const int hd_scale = c.surface->hd_scale();
    const bool use_hd_text = c.surface->use_hd_text();
    const Uint32 frame_ms = c.frame_ms;
    const int prev_screen = c.prev_screen;
    const int logical_w = c.logical_w;
    const int logical_h = c.logical_h;
    const int kL3SmokeTailTicks = c.l3_smoke_tail_ticks;
    auto upload_and_show = [&](FrameBuffer& f, bool with_hud = true,
                               bool do_present = true) {
        c.upload_and_show(f, with_hud, do_present);
    };
    // ── Descent widescreen margins (#1) ──────────────────────
    // The trunk-descent composes a NATIVE 320x200 frame per step
    // and hands it to a present callback.  The default callback
    // (upload_and_show) pillarboxes that native frame with pure-
    // black bars under widescreen, so the margins VANISH for the
    // whole animation.  Instead, composite each native frame into
    // the bound screen's static wide background (backdrop + ground
    // extended to the edge — the SAME no-neighbour fill the steady
    // screen-18 view uses) so the bezel stays filled and the
    // descent ends seamlessly into the widescreen surface.  Pure
    // rendering: no LCG/state touched (the smoke jitter is rolled
    // logic-side), so the rng-critical descent is unaffected.
    // Rebuilt per phase because Phase 1 is bound to screen 17 and
    // the pan + Phase 2 to screen 18.
    std::vector<std::uint8_t> descent_wide;   // native wsp.native_w()x200
    // Phase-1 (screen 17) and Phase-2 (screen 18) wide margins, kept
    // so the camera pan can scroll BOTH (screen 17 up, screen 18 in
    // from below) in lockstep with the centre — no margin pop.
    std::vector<std::uint8_t> descent_m17, descent_m18;
    const bool descent_ws =
        wsp.active() && hd && hd_scale > 1 && wsp.wide_tex() != nullptr;
    // PACING IS PER STAGE, because only one stage interpolates.
    //
    //   Phase 1 / Phase 2 — one frame per 18 Hz logical step, paced at the
    //     full frame_ms.  Their y_offset is integer per logical frame, so the
    //     sub-frames they used to render were byte-identical duplicates, each
    //     paying a full wide omniscale upscale.  See the substeps comments in
    //     run_l3_screen17_descent / run_l3_trunk_descent.
    //   Camera pan — genuinely continuous, so it keeps `substeps` sub-frames
    //     and must be paced at frame_ms/substeps or its glide is starved.
    //
    // Historic trap this deliberately does NOT re-create: pacing sub-frames at
    // the full frame_ms ran the descent 3x too slow (the "pathetic" perf).
    // That is only a trap while a stage HAS sub-frames — which now, apart from
    // the pan, none does.
    const Uint32 descent_pan_substeps =
        static_cast<Uint32>(opts.enhanced ? 3 : 1);   // must match run_l3_descent_pan
    Uint32 descent_step_ms = frame_ms;                // Phase 1 / Phase 2
    // F5 during the descent: the blocking descent loop never reaches
    // the frame-service bug-capture, so capture the full WS composite
    // here instead (standalone PNG — no g.state frame service).
    int descent_shot_seq = 0;
    bool descent_shot = false;
    bool descent_prev_f5 = false;   // F5 rising-edge latch
    auto build_descent_margins = [&]() {
        if (!descent_ws) return;
        // Build the descent margins EXACTLY like the steady view
        // (get_static_wide_bg_hd): the SAME neighbour peeks
        // (ws_left/ws_right for the bound screen), so the margins are
        // identical to the steady screen before and after the descent
        // — no pop-in/out at the boundaries.  Using REAL neighbours
        // also kills the earlier bark strip: Phase-1's right margin is
        // a peek of screen 18, not a no-neighbour MIRROR of screen
        // 17's foreground tree column.  Only a genuine level-edge
        // (screen 18's right) falls back to the layer extension.
        wsp.compose_static_wide_bg(descent_wide);
    };
    auto present_ws_descent = [&](const FrameBuffer& f,
                                  bool with_hud) -> bool {
        const Uint32 t0 = SDL_GetTicks();
        // ESC is inert during the L3 trunk-descent cinematic (no menu here) —
        // it used to silently abort the whole run to title.  Only a real
        // window-close stops it.
        if (!poll_screen_events(win)) return false;
        // F5 is consumed by the descent function's OWN SDL_PollEvent
        // loop (it runs before this present callback), so the keydown
        // never reaches the drain above.  Read the key STATE directly
        // (level-triggered, not queue-drained) with a rising-edge
        // latch so one press = one capture.
        {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            const bool f5 = ks != nullptr && ks[SDL_SCANCODE_F5] != 0;
            if (f5 && !descent_prev_f5) descent_shot = true;
            descent_prev_f5 = f5;
        }
        const std::size_t need =
            static_cast<std::size_t>(wsp.native_w()) * 200 * 4;
        if (!descent_ws || descent_wide.size() != need) {
            // Classic / non-widescreen: pillarbox via upload_and_show.
            FrameBuffer copy = f;
            upload_and_show(copy, with_hud);
            const Uint32 el = SDL_GetTicks() - t0;
            if (el < descent_step_ms) SDL_Delay(descent_step_ms - el);
            return true;
        }
        // Composite the native descent frame into the static margins.
        std::vector<std::uint8_t> wide = descent_wide;
        for (int y = 0; y < 200; ++y)
            std::memcpy(
                &wide[(static_cast<std::size_t>(y) * wsp.native_w() +
                       wsp.margin()) * 4],
                &f.px[static_cast<std::size_t>(y) * 320 * 4],
                320 * 4);
        // Neighbour seam overhang into the centre: descent_wide's
        // margins carry the straddler completion, but the descent
        // frame `f` is composed natively (320) without the
        // neighbour's tiles, so the memcpy above buried it —
        // re-apply band-limited (shared helper; Phase-2/pan seam
        // lists are empty, so those present calls no-op).
        wsp.reapply_seam_bands(wide);
        enhance::EnhancedHudLayout hud_layout;
        const bool hud = with_hud && use_hd_text;
        if (hud) {
            hud_layout =
                enhance::compute_enhanced_hud_layout(hd_text, g.state);
            enhance::draw_enhanced_hud_bars(wide, wsp.native_w(), 200, 1,
                                            hud_layout, wsp.margin());
        }
        // OLDUVAI_DUMP_DESCENT: dump the composed WS descent frame
        // (pre-upscale native_wx200) so the l3_end_level extraction
        // is provable byte-identical.  Debug-only; moves with
        // present_ws_descent into l3_end_level.cpp.
        if (const char* dd = std::getenv("OLDUVAI_DUMP_DESCENT")) {
            static int descent_dump_seq = 0;
            char p[512];
            std::snprintf(p, sizeof p, "%s/descent_%04d.bmp", dd,
                          descent_dump_seq++);
            save_rgba_image(wide.data(), wsp.native_w(), 200, p);
        }
        std::vector<std::uint8_t> up = enhance::upscale_rgba(
            wide, wsp.native_w(), 200, hd_scale, opts.hd_profile);
        SDL_UpdateTexture(wsp.wide_tex(), nullptr, up.data(),
                          wsp.native_w() * hd_scale * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, wsp.wide_tex(), nullptr, nullptr);
        if (hud && hd_text.ok()) {
            int ow = 0, oh = 0;
            if (text_overlay.begin(ren, hd_text, ow, oh)) {
                wsp.draw_wide_hud_text(text_overlay.buffer(), ow,
                                       oh, hud_layout);
                text_overlay.flush(ren, logical_w, logical_h);
            }
        }
        if (descent_shot) {   // F5 pressed mid-descent — save full WS
            descent_shot = false;
            int ow = 0, oh = 0;
            SDL_GetRendererOutputSize(ren, &ow, &oh);
            SDL_Surface* sh = SDL_CreateRGBSurfaceWithFormat(
                0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
            if (sh != nullptr &&
                SDL_RenderReadPixels(ren, nullptr,
                    SDL_PIXELFORMAT_RGBA32, sh->pixels,
                    sh->pitch) == 0) {
                // Same root as the F5 report dirs (this IS the
                // descent's F5 capture) — never the cwd.
                const std::filesystem::path root =
                    bug_report_root();
                std::error_code ec;
                std::filesystem::create_directories(root, ec);
                char p[64];
                std::snprintf(p, sizeof p, "descent_ws_%03d.png",
                              descent_shot_seq++);
                const std::string path = (root / p).string();
                save_surface_image(sh, path);
                std::fprintf(stderr, "[WS-SHOT] saved %s\n",
                             path.c_str());
            }
            if (sh != nullptr) SDL_FreeSurface(sh);
        }
        SDL_RenderPresent(ren);
        const Uint32 work = SDL_GetTicks() - t0;
        if (work < descent_step_ms) SDL_Delay(descent_step_ms - work);
        return true;
    };
    auto descent_present = [&](const FrameBuffer& f) -> bool {
        return present_ws_descent(f, /*with_hud=*/false);
    };
    // Phase 1 (FUN_2276_0282): run BEFORE bind_screen so the
    // animation shows screen 17's platform sliding down against
    // screen 17's backdrop.  LCG jitter is rolled logic-side
    // AFTER Phase 1 and BEFORE Phase 2 (reference ordering:
    // step 8c — roll_l3_descent_smoke_jitter then
    // run_l3_trunk_descent).
    //
    // Headless / replay: Phase 1 and Phase 2 animations are
    // blocking display loops — in headless (opts.frames > 0)
    // the present lambda already returns immediately, so they
    // burn through frames quickly.  The LCG consumption
    // (jitter roll = 40 draws) MUST happen regardless.
    //
    // Build the L3-surface-specific sprite vectors needed by
    // the descent renderer (already loaded into g.render / g.grot3).
    // The descent functions compose into a NATIVE 320x200 buffer
    // and pass it to `present`; `present` then upscales for HD.
    // We must NOT pass the HD-sized gameplay fb directly — the
    // descent does `fb = bg_static` (native copy) which would
    // resize it and corrupt subsequent gameplay composition.
    {
        // g.render.tile_sprites = ELEML3(28) + ELEML3B(5) + GROT3(2)
        // (appended in bind_screen L3 surface path).
        // Decoration tiles use indices 0/1 (= ELEML3[0/1]).
        // The transition already advanced current_screen to 18, but
        // these are SCREEN-17's margins — restore it to 17 so the
        // per-screen dead-end rules (trunk-skip + dirt-void in
        // compose_static_wide_bg_native) fire; otherwise the giant
        // trunk spills into the strip for the whole descent.
        const int saved_cs = g.state.current_screen;
        g.state.current_screen = prev_screen;   // 17
        build_descent_margins();   // screen 17's static wide margins
        descent_m17 = descent_wide;   // saved for the pan scroll
        g.state.current_screen = saved_cs;       // back to 18
        FrameBuffer native_fb{};   // native 320x200 for descent
        if (!run_l3_screen17_descent(
                L3DescentPhase{g.state, g.render.tile_sprites,
                               g.render.entity_sprites, g.render.palette,
                               g.tiles, g.grot3, native_fb, opts.enhanced,
                               g.render.extend_top_backdrop,
                               descent_present})) {
            running = false;
        }
    }
    // Roll smoke jitter logic-side — 40 LCG draws consumed in
    // the same order as the EXE regardless of rendering mode.
    // FUN_2276_03d9:0x0554 (smoke A) + 0x0586 (smoke B), iters 0..19.
    systems::roll_l3_descent_smoke_jitter(g.state);

    // Bind screen 18 now (before Phase 2).  Phase 2 renders
    // screen-17 records descending into screen-18's backdrop.
    systems::clear_per_screen_state(g.state);
    bind_screen(g, g.state.current_screen);
    wsp.update_cache();   // recompute peek for the new screen
    build_descent_margins();     // screen 18's static wide margins
    descent_m18 = descent_wide;  // saved for the pan scroll
    g.state.screen_change = false;
    g.state.transition_skip = true;

    // Enhancement #11 (opt-in, NOT EXE-faithful): camera-pan
    // bridging Phase 1 → Phase 2.  The EXE hard-swaps the backdrop
    // here; this glides screen 17 up/out and screen 18 in from
    // below.  Gated on opts.enhanced inside the helper
    // (no-op + early return when the flag is off → the default path
    // is byte-identical).  The pan touches NO LCG/RNG state — it
    // builds backdrops from the already-bound L3 surface tiles and
    // scrolls them — so the trace/corpus is unaffected.  Runs AFTER
    // bind_screen(18) so screen 18's tiles are available for the
    // incoming surface, mirroring the reference where
    // the pan precedes Phase 2.  Smoke jitter was already rolled
    // above; pan order relative to the roll is irrelevant (pan = 0
    // LCG draws).
    //
    // present_hud: like `present` but draws the enhanced HUD over
    // each pan frame (with_hud=true) so the HUD stays visible across
    // the pan — reference _present_frame draws the HUD every pan
    // frame.  No state mutation:
    // draw_enhanced_hud_* only reads g.state.
    if (running && opts.enhanced) {
        // present_hud: the descent present WITH the enhanced HUD on
        // each pan frame.  Also scrolls the wide MARGINS in lockstep
        // with the centre pan — screen-17 margins recede UP, screen-18
        // margins enter from BELOW, the SAME geometry run_l3_descent_
        // pan uses for the centre (ody=-t*H, ndy=H+ody) — so the bezel
        // transitions screen 17 → 18 seamlessly with the centre
        // instead of popping at the phase boundary.
        const int pan_n = 12 * (opts.enhanced ? 3 : 1);
        int pan_i = 0;
        auto present_hud = [&](const FrameBuffer& f) -> bool {
            if (descent_ws &&
                descent_m17.size() == descent_wide.size() &&
                descent_m18.size() == descent_wide.size()) {
                const double t =
                    static_cast<double>(++pan_i) / pan_n;
                const int H = 200;
                const int ody = -static_cast<int>(t * H);
                const int ndy = H + ody;
                const std::size_t rowb =
                    static_cast<std::size_t>(wsp.native_w()) * 4;
                std::fill(descent_wide.begin(), descent_wide.end(), 0);
                auto shift = [&](const std::vector<std::uint8_t>& src,
                                 int sdy) {
                    for (int y = 0; y < H; ++y) {
                        const int sy = y - sdy;
                        if (sy < 0 || sy >= H) continue;
                        std::memcpy(
                            &descent_wide[static_cast<std::size_t>(y) *
                                          rowb],
                            &src[static_cast<std::size_t>(sy) * rowb],
                            rowb);
                    }
                };
                shift(descent_m17, ody);   // screen 17 recedes up
                shift(descent_m18, ndy);   // screen 18 enters below
            }
            return present_ws_descent(f, /*with_hud=*/true);
        };
        FrameBuffer native_pan{};   // native 320x200 for the pan
        // The pan is the ONE stage with real sub-frames: pace it at the
        // sub-frame budget so the camera glides, then hand the full-frame
        // budget back to Phase 2 below.
        descent_step_ms = frame_ms / descent_pan_substeps;
        const bool pan_ok = run_l3_descent_pan(
            L3DescentPhase{g.state, g.render.tile_sprites,
                           g.render.entity_sprites, g.render.palette,
                           g.tiles, g.grot3, native_pan, opts.enhanced,
                           g.render.extend_top_backdrop, present_hud});
        descent_step_ms = frame_ms;
        if (!pan_ok) {
            running = false;
        }
        descent_wide = descent_m18;   // Phase 2 = static screen-18
    }

    // Phase 2 (FUN_2276_03d9): 21 iters, y_offset -80→0.
    if (running) {
        const auto& td = g.tiles;
        FrameBuffer native_fb2{};   // native 320x200 for Phase 2
        if (!run_l3_trunk_descent(
                L3DescentPhase{g.state, g.render.tile_sprites,
                               g.render.entity_sprites, g.render.palette,
                               td, g.grot3, native_fb2, opts.enhanced,
                               g.render.extend_top_backdrop,
                               descent_present})) {
            running = false;
        } else {
            // Stamp the descent-overlay tiles onto screen 18's
            // collision + render list (mirrors EXE
            // FUN_2276_03d9 Collision_StampDur on iter 20;
            // reference: _setup_screen_collision after run_l3_trunk_descent).
            std::vector<presentation::LevelRenderAssets::TileDraw>
                overlay;
            for (const auto& tp : l3_descent_overlay_tiles(td)) {
                const int idx = descent_resolve_sprite_idx(tp.sprite_idx);
                if (idx >= 0 &&
                    idx < static_cast<int>(g.dur.tiles.size())) {
                    g.state.collision.stamp_tile(
                        g.dur.tiles[static_cast<std::size_t>(idx)].segments,
                        tp.x, tp.y);
                }
                overlay.push_back({idx, tp.x, tp.y});
            }
            // Draw order matches the reference:
            // the overlay renders BEFORE screen 18's own records,
            // so the ground row covers the descended trunk's
            // bottom — the trunk stays BEHIND the ground exactly
            // as during the descent animation.  Appending it
            // (drawn last) popped the trunk bottom OVER the
            // ground on the first steady frame.  Insert after the
            // bind-injected backdrop, before the level tiles.
            const auto ins =
                g.render.tiles.begin() +
                std::min<std::ptrdiff_t>(
                    g.render.backdrop_tile_count,
                    static_cast<std::ptrdiff_t>(
                        g.render.tiles.size()));
            g.render.tiles.insert(ins, overlay.begin(),
                                  overlay.end());
            // Enhanced descent package: arm the settling-dust tail
            // on the steady screen (same gate as the in-animation
            // dust extension).
            if (opts.enhanced)
                l3_smoke_tail = kL3SmokeTailTicks;
        }
    }
}
}  // namespace olduvai::presentation
