// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// L3 (Dark Woods) trunk-descent end-level sequence.
// Port of FUN_2276_0282 (Phase 1, 375 B) and FUN_2276_03d9 (Phase 2, 691 B).
//
// EXE capstone sources:
//   FUN_2276_0282 walk (Phase 1)
//   FUN_2276_03d9 walk (Phase 2)
// Finding: l3 screen-18 trunk-descent (internal research notes)

#include "presentation/l3_end_level.hpp"

#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

#include "presentation/game_render.hpp"
#include "presentation/tile_patterns.hpp"

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
// y_offset starts at −80 (init value 0xffb0 at 0x03e1).
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

bool run_l3_screen17_descent(
    systems::SystemsState& state,
    const std::vector<formats::Sprite>& tile_sprites,
    const std::vector<formats::Sprite>& entity_sprites,
    const std::vector<formats::Rgb>& palette,
    const prepare::LevelTiles& tile_data,
    const std::vector<formats::Sprite>& grot3,
    FrameBuffer& fb,
    bool enhanced,
    bool extend_band,
    const std::function<bool(const FrameBuffer&)>& present)
{
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

    // Pacing: 44 iters × 4 ticks = 176 render frames at 18 Hz (≈ 9.7 s).
    // Classic: 1 sub-frame per iter-tick (one compose per 4-tick wait).
    // Enhanced smooth-motion: 3 sub-frames per logical 4-tick step.
    const int substeps = enhanced ? 3 : 1;
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

        // Drain SDL events — allow ESC abort.
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { state.game_over = true; return false; }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                state.game_over = true;
                return false;
            }
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

bool run_l3_trunk_descent(
    systems::SystemsState& state,
    const std::vector<formats::Sprite>& tile_sprites,
    const std::vector<formats::Sprite>& entity_sprites,
    const std::vector<formats::Rgb>& palette,
    const prepare::LevelTiles& tile_data,
    const std::vector<formats::Sprite>& grot3,
    FrameBuffer& fb,
    bool enhanced,
    bool extend_band,
    const std::function<bool(const FrameBuffer&)>& present)
{
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

    // Pacing: 21 iters × 4 ticks = 84 logical frames at 18 Hz (≈ 4.7 s).
    const int substeps = enhanced ? 3 : 1;
    const int total_frames = kS18Iters * kTicksPerIter;   // 84

    for (int frame = 0; frame < total_frames * substeps; ++frame) {
        // y_offset: −80 + (logical frame index), clamped to 0.
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
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                state.game_over = true;
                return false;
            }
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
// Composes native 320×200 surfaces and scrolls them with a `blit_shifted`-style
// 'down' pan (same geometry as game_app.cpp kind-3 'D' slide), presenting each
// frame via `present` (which the caller wires to draw the enhanced HUD so the
// HUD stays visible across the pan).
bool run_l3_descent_pan(
    systems::SystemsState& state,
    const std::vector<formats::Sprite>& tile_sprites,
    const std::vector<formats::Sprite>& entity_sprites,
    const std::vector<formats::Rgb>& palette,
    const prepare::LevelTiles& tile_data,
    const std::vector<formats::Sprite>& grot3,
    FrameBuffer& fb,
    bool enhanced,
    bool extend_band,
    const std::function<bool(const FrameBuffer&)>& present)
{
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
    FrameBuffer s17{};   // native 320×200
    build_l3_bg_base(s17, tile_sprites, palette, grot3);
    blit_screen_tiles(s17, s17tiles, kDescendTiles, tile_sprites, palette,
                      extend_band);
    FrameBuffer s18{};   // native 320×200
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

}  // namespace olduvai::presentation
