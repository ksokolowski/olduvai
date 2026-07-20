// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/screen_tiles.hpp"

#include <algorithm>

#include "core/constants.hpp"
#include "presentation/tile_patterns.hpp"

namespace olduvai::presentation {

// L3 tile-sprite alias chain (0-based; -1 = skip draw + collision).
int resolve_sprite_idx(int level, int idx) {
    if (level != 3) return idx;
    switch (idx) {
        case 29: return 30;
        case 28: return -1;   // skip
        case 19: return 31;   // pine silhouette
        case 4: return 28;
        default: return idx;
    }
}

int resolve_tile_screen(int level, int screen) {
    // L1 screen tile alias (11 -> 10); other levels none.
    if (level == 1 && screen == 11) return 10;
    return screen;
}

void normalize_glider_water(std::vector<LevelRenderAssets::TileDraw>& tiles,
                            int baseline) {
    int surf = 1 << 30;
    for (const auto& t : tiles)
        if (t.sprite_idx == 7) surf = std::min(surf, t.y);
    if (surf == (1 << 30)) return;          // no water body on this screen
    const int delta = baseline - surf;
    for (auto& t : tiles) {
        if (t.sprite_idx == 7)
            t.y = baseline;                 // flatten the body/foam surface
        else if (t.sprite_idx == 16 || t.sprite_idx == 17 || t.sprite_idx == 18)
            t.y += delta;                   // keep sub-surface detail below foam
        else if (t.sprite_idx == 3)         // the "LEVEL n" signpost in the water
            // delta brings the post's icy base back to the (lowered) waterline;
            // -5 lifts it so the base RESTS on the surface rather than dipping in
            // (chosen from a 0/-3/-5/-7 comparison — full delta sank it).
            t.y += delta - 5;
    }
}

void l7_bridge_ceiling_to_wall(
    const prepare::LevelTiles& level_tiles, int screen,
    std::vector<LevelRenderAssets::TileDraw>& tiles) {
    if (screen < 0 || screen >= static_cast<int>(level_tiles.screens.size()))
        return;
    int ceiling = 0, cmax = -(1 << 28), wall = 1 << 28;
    for (const auto& tp :
         level_tiles.screens[static_cast<std::size_t>(screen)].tiles) {
        if (tp.y != 9) continue;
        if (tp.sprite_idx == 7) {
            ++ceiling;
            cmax = std::max(cmax, tp.x + 48);
        } else if (tp.sprite_idx == 6) {
            cmax = std::max(cmax, tp.x + 16);
        } else if (tp.sprite_idx == 3 || tp.sprite_idx == 4) {
            wall = std::min(wall, tp.x);
        }
    }
    if (ceiling < 3 || wall == (1 << 28) || cmax >= wall + 16) return;
    tiles.push_back({7, cmax, 9});
    for (const auto& tp :
         level_tiles.screens[static_cast<std::size_t>(screen)].tiles)
        if (tp.y == 9 && tp.sprite_idx == 4 && tp.x >= cmax)
            tiles.push_back({4, tp.x, 9});
}

int build_screen_tiles(const ScreenTileContext& ctx, int screen,
                       std::vector<LevelRenderAssets::TileDraw>& out) {
    out.clear();
    const int level = ctx.level;
    const auto& screens = ctx.level_tiles->screens;
    const int tile_screen = resolve_tile_screen(level, screen);

    // Per-level background tiling (drawn before placements).
    if (level == 3) {
        // Net effect of the EXE draw->black-fill->pine order:
        // the trunk shows only on screens 10/11; every other L3 screen is the
        // dark backdrop + pine silhouettes.
        if (screen == 10 || screen == 11) {
            // GROT3 trunk column at x=98 — visible only on screens 10 and 11.
            // Indices surface_tile_count/+1 into the atlas (GROT3[0] body /
            // [1] cap — 33/34 with the stock 33-tile surface sheet).
            const int kGrot3Body = ctx.surface_tile_count;
            const int kGrot3Cap  = kGrot3Body + 1;
            if (static_cast<int>(ctx.tile_sprites->size()) > kGrot3Cap) {
                constexpr int kTrunkX = 98;
                for (int ty : {167, 128, 89, 50})
                    out.push_back({kGrot3Body, kTrunkX, ty});
                out.push_back({kGrot3Cap, kTrunkX, 23});
            }
        } else {
            // All other L3 surface screens: dark backdrop + pine silhouettes only.
            out.push_back({31, 0, 9});
            out.push_back({31, 160, 9});
        }
    } else if (level == 7) {
        constexpr int xs[5] = {0, 64, 128, 192, 256};
        if (screen < 10 || screen > 12) {
            // Lavarock backdrop ELEML7[19] (64x63) tiled 5x3 at y=9/72/135.
            // Enhanced HD: prepend a row at y=9-63=-54 so the SAME seamless
            // tiling continues up through the top HUD-strip band (rows 0..8) —
            // the real backdrop, not a mirror.  The tile reaches both screen
            // edges so compose_static_wide_bg_native's row-continuation carries
            // it into the widescreen margins too.  (The compose-time mirror is
            // gated to PC1 levels, so it never runs for L7 — no conflict.)
            if (ctx.extend_top_backdrop)
                for (int tx : xs) out.push_back({19, tx, -54});
            for (int ty : {9, 72, 135})
                for (int tx : xs) out.push_back({19, tx, ty});
            // Ceiling screens (16..18 author the full-width stalactite border
            // ELEML7[7] at y=9): the band above the ceiling is SOLID ROCK, not
            // open lavarock — with only the backdrop row the stalactites hang
            // mid-air under lava in the transparent HUD band.  Push a gray
            // rock row (ELEML7[4], 48x30) at y=-21 covering rows 0..8, over
            // the lavarock row, still backdrop-layer (level tiles draw above).
            // Reaches both edges -> the margin row-continuation carries it.
            if (ctx.extend_top_backdrop &&
                screen >= 0 &&
                screen < static_cast<int>(screens.size())) {
                // (bounds guard: a tampered save can reach here with an
                // out-of-range surface screen)
                int ceiling = 0;
                for (const auto& tp : screens[static_cast<std::size_t>(
                         screen)].tiles)
                    if (tp.sprite_idx == 7 && tp.y == 9) ++ceiling;
                // Follow the AUTHORED top row, not the full width: S16's
                // ceiling starts mid-screen at x=64 (end-cap spr 6) — a
                // 0..320 band floated a gray strip over its open left part
                // (and over the S15 peek).  One rock tile above each
                // authored y=9 tile covers exactly the ceiling/cap/wall-top
                // extent.
                // Ceiling screens get the FULL S17 treatment (user rule:
                // one consistent top look).  Full-width gray band, and where
                // the authored run starts mid-screen (S16: cap at x=64) the
                // gap to the left edge is FILLED with synthetic stalactite
                // tiles — a partial band/run start read as an orphaned
                // fragment both on the screen and in the S15 peek corner.
                // The leftmost synthetic tile is clamped to x=0 (never a
                // straddler, so it can't leak into the neighbour's centre
                // via the seam lists).
                if (ceiling >= 3) {
                    for (int tx = 0; tx < 320; tx += 48)
                        out.push_back({4, tx, -21});
                    int cmin = 1 << 28;
                    for (const auto& tp : screens[static_cast<
                             std::size_t>(screen)].tiles)
                        if (tp.y == 9 &&
                            (tp.sprite_idx == 7 || tp.sprite_idx == 6))
                            cmin = std::min(cmin, tp.x);
                    if (cmin != (1 << 28) && cmin > 0)
                        for (int x = cmin - 48;;) {
                            if (x < 0) x = 0;
                            out.push_back({7, x, 9});
                            if (x == 0) break;
                            x -= 48;
                        }
                }
            }
        } else {
            // L7 lava-cave-warp area (screens 10-12): visible floor/ceiling
            // render strips (surface_tiles sprites).  The matching CONTINUOUS
            // COLLISION floor (EXE FUN_25b2_000c DUR idx 29 stamps) is a
            // side-effect and stays at the bind_screen call site.
            for (int tx : xs) {
                out.push_back({29, tx, 79});
                out.push_back({31, tx, 40});
            }
        }
    }
    // Everything pushed so far is bind-injected BACKDROP; what follows is
    // level data (+ the top-extension appends).  The widescreen seam pass
    // uses this split to layer a neighbour's seam overhang UNDER level tiles.
    const int backdrop_tile_count = static_cast<int>(out.size());
    if (tile_screen >= 0 &&
        tile_screen < static_cast<int>(screens.size())) {
        for (const auto& tp : screens[static_cast<std::size_t>(
                 tile_screen)].tiles) {
            const int idx = resolve_sprite_idx(level, tp.sprite_idx);
            if (idx < 0) continue;   // alias chain says skip
            out.push_back({idx, tp.x, tp.y});
        }
    }
    if (level == 7 && ctx.extend_top_backdrop)
        l7_bridge_ceiling_to_wall(*ctx.level_tiles, tile_screen, out);
    // Enhanced icy-glider sea-level normalisation (glider_water_y, set only
    // for the icy level under --enhanced): the decorative water (sprite 7,
    // 0 collision segments — death is the fixed y>180 fall, not water-touch) is
    // authored at a rising/falling Y per flight screen.  Flatten it to a single
    // Y across the flight screens (9..last) so the sea reads as one continuous
    // body during the glider, instead of stepping up mid-flight and back down at
    // the landing.  Purely visual (water has no collision); faithful path keeps
    // glider_water_y = -1.
    if (ctx.glider_water_y >= 0 && screen >= 9 && screen <= core::kLastScreen)
        normalize_glider_water(out, ctx.glider_water_y);
    // Enhanced (tile-composed levels — Dark Woods L3, Volcanic L7): a vertical
    // column (forest trunk, giant trunk, rock pillar) whose top segment lands
    // near the playfield top reads as "cut off" with sky/black above it in the
    // transparent-HUD widescreen view.  Continue such columns up to row 0 by
    // reusing the column's own repeat pattern (tile_patterns spike).  PC1
    // levels (L1/icy) get the compose-time sky mirror instead.
    if (ctx.extend_top_backdrop && !ctx.visual_background)
        tile_patterns::extend_columns_to_top(out, *ctx.tile_sprites);
    return backdrop_tile_count;
}

}  // namespace olduvai::presentation
