// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/screen_tiles.hpp"

#include <algorithm>

#include "core/constants.hpp"
#include "presentation/render/tile_patterns.hpp"

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
            // Bring the post's icy base to the lowered waterline; -5 rests it
            // on the surface (the full delta sank it).
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

// ---- Per-level backdrop recipes (read-only, deterministic, no RNG) ----

// L3: the GROT3 trunk column on screens 10/11, pine silhouettes elsewhere (the
// net effect of the EXE's draw -> black-fill -> pine order).
void push_l3_backdrop(const ScreenTileContext& ctx, int screen,
                      std::vector<LevelRenderAssets::TileDraw>& out) {
    if (screen == 10 || screen == 11) {
        // GROT3 at x=98: body/cap at surface_tile_count/+1 (33/34 with the
        // stock sheet).
        const int kGrot3Body = ctx.surface_tile_count;
        const int kGrot3Cap  = kGrot3Body + 1;
        if (static_cast<int>(ctx.tile_sprites->size()) > kGrot3Cap) {
            constexpr int kTrunkX = 98;
            for (const int ty : {167, 128, 89, 50})
                out.push_back({kGrot3Body, kTrunkX, ty});
            out.push_back({kGrot3Cap, kTrunkX, 23});
        }
    } else {
        // All other L3 surface screens: dark backdrop + pine silhouettes only.
        out.push_back({31, 0, 9});
        out.push_back({31, 160, 9});
    }
}

// L7 ceiling screens (16..18, stalactite border at y=9): the band above the
// ceiling is solid rock; with only the lavarock row the stalactites hang under
// lava in the transparent HUD band.
void push_l7_ceiling_band(const ScreenTileContext& ctx, int screen,
                          std::vector<LevelRenderAssets::TileDraw>& out) {
    const auto& screens = ctx.level_tiles->screens;
    if (ctx.extend_top_backdrop &&
        screen >= 0 &&
        screen < static_cast<int>(screens.size())) {
        // Bounds guard: a tampered save can reach here with a bad screen.
        int ceiling = 0;
        for (const auto& tp : screens[static_cast<std::size_t>(
                 screen)].tiles)
            if (tp.sprite_idx == 7 && tp.y == 9) ++ceiling;
        // Ceiling screens: a full-width gray band, and where the authored run
        // starts mid-screen (S16's cap at x=64) the gap to the left edge is
        // filled with stalactite tiles, so no fragment shows on screen or in
        // the S15 peek.  The leftmost tile is clamped to x=0 (never a seam
        // straddler).
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
}

// L7: the lavarock backdrop (+ the rock band on ceiling screens); screens
// 10-12 (cave-warp area) get their floor/ceiling strips instead.  Their
// collision floor is stamped at the bind_screen call site.
void push_l7_backdrop(const ScreenTileContext& ctx, int screen,
                      std::vector<LevelRenderAssets::TileDraw>& out) {
    constexpr int xs[5] = {0, 64, 128, 192, 256};
    if (screen < 10 || screen > 12) {
        // Lavarock ELEML7[19] (64x63) tiled 5x3 at y=9/72/135.  Enhanced: an
        // extra row at y=-54 continues the real tiling through the HUD band; it
        // reaches both edges, so the margin row continuation carries it.
        if (ctx.extend_top_backdrop)
            for (const int tx : xs) out.push_back({19, tx, -54});
        for (const int ty : {9, 72, 135})
            for (const int tx : xs) out.push_back({19, tx, ty});
        // Ceiling screens: a gray rock row (ELEML7[4], 48x30) at y=-21 covers
        // rows 0..8 above the lavarock, still backdrop layer.
        push_l7_ceiling_band(ctx, screen, out);
    } else {
        // Screens 10-12: visible floor/ceiling strips.  Their collision floor
        // (DUR idx 29 stamps, FUN_25b2_000c) is stamped at the bind_screen call
        // site.
        for (const int tx : xs) {
            out.push_back({29, tx, 79});
            out.push_back({31, tx, 40});
        }
    }
}

int build_screen_tiles(const ScreenTileContext& ctx, int screen,
                       std::vector<LevelRenderAssets::TileDraw>& out) {
    out.clear();
    const int level = ctx.level;
    const auto& screens = ctx.level_tiles->screens;
    const int tile_screen = resolve_tile_screen(level, screen);

    // Per-level background tiling (drawn before placements).
    if (level == 3) {
        push_l3_backdrop(ctx, screen, out);
    } else if (level == 7) {
        push_l7_backdrop(ctx, screen, out);
    }
    // Everything so far is backdrop; level data follows.  The widescreen seam
    // pass layers a neighbour's overhang between the two.
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
    // Enhanced L5 glider: flatten the decorative water (sprite 7, no collision;
    // death is the y>180 fall) to one Y across flight screens 9..last, so the
    // sea does not step.  Classic keeps glider_water_y = -1.
    if (ctx.glider_water_y >= 0 && screen >= 9 && screen <= core::kLastScreen)
        normalize_glider_water(out, ctx.glider_water_y);
    // Enhanced tile levels (L3, L7): continue vertical columns (trunks,
    // pillars) whose top lands near row 0 up to the top, so they are not cut
    // off under the transparent HUD.  PC1 levels use the compose-time sky
    // mirror.
    if (ctx.extend_top_backdrop && !ctx.visual_background)
        tile_patterns::extend_columns_to_top(out, *ctx.tile_sprites);
    return backdrop_tile_count;
}

}  // namespace olduvai::presentation
