// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OL-B2 identity gate — build_screen_tiles vs the frozen pre-refactor
// construction, on REAL game data, for EVERY surface screen of every
// tile-composed level (1/3/5/7), in both faithful and enhanced
// (extend_top_backdrop / glider-water) configurations.
//
// Before OL-B2, bind_screen and build_surface_screen_assets each carried a
// hand-mirrored copy of this construction and the copies had begun to drift
// (2026-07-03 critical review, Tier-3 #2).  Both now call the ONE pure
// constructor, so bind-vs-peek identity holds by construction; what still
// needs proving is that the unified constructor is byte-identical to what
// both paths produced BEFORE the refactor.  `golden_build` below is that
// pre-refactor construction, frozen verbatim (the branch/backdrop/placement
// logic that was duplicated; the shared leaf helpers — l7 bridge, glider
// water, column extension — are called through their now-public entry
// points, exactly as both old copies did).
//
// If build_screen_tiles legitimately changes, update golden_build in the
// same commit — the pair going through review together is the drift guard.
//
// Data-gated: exits 77 (CTest SKIP_RETURN_CODE) when game data is absent,
// following the golden_trace.sh pattern.  Game dir: $OLDUVAI_GAME_DATA,
// falling back to the repo-local game_data (gitignored; symlink it
// to wherever your copy lives).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/constants.hpp"
#include "formats/cur.hpp"
#include "formats/mat.hpp"
#include "prepare/exe_tables.hpp"
#include "presentation/screen_tiles.hpp"
#include "presentation/tile_patterns.hpp"

namespace fs = std::filesystem;
using olduvai::formats::CurArchive;
using olduvai::formats::MatFile;
using olduvai::formats::Sprite;
using olduvai::presentation::LevelRenderAssets;
using TileDraw = LevelRenderAssets::TileDraw;

namespace {

std::vector<std::uint8_t> slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

// ── Frozen pre-refactor construction (game_app.cpp @ 0888342) ──────────────

// L3 tile-sprite alias chain (0-based; -1 = skip draw + collision).
int golden_resolve_sprite_idx(int level, int idx) {
    if (level != 3) return idx;
    switch (idx) {
        case 29: return 30;
        case 28: return -1;   // skip
        case 19: return 31;   // pine silhouette
        case 4: return 28;
        default: return idx;
    }
}

struct Golden {
    std::vector<TileDraw> tiles;
    int backdrop_tile_count = 0;
};

Golden golden_build(const olduvai::prepare::LevelTiles& level_tiles,
                    const std::vector<Sprite>& tile_sprites,
                    int level, int screen, int surface_tile_count,
                    bool extend_top_backdrop, bool visual_background,
                    int glider_water_y) {
    Golden g;
    auto& tiles = g.tiles;
    const auto& screens = level_tiles.screens;

    int tile_screen = screen;
    if (level == 1 && screen == 11) tile_screen = 10;

    if (level == 3) {
        if (screen == 10 || screen == 11) {
            const int kGrot3Body = surface_tile_count;
            const int kGrot3Cap = kGrot3Body + 1;
            if (static_cast<int>(tile_sprites.size()) > kGrot3Cap) {
                constexpr int kTrunkX = 98;
                for (int ty : {167, 128, 89, 50})
                    tiles.push_back({kGrot3Body, kTrunkX, ty});
                tiles.push_back({kGrot3Cap, kTrunkX, 23});
            }
        } else {
            tiles.push_back({31, 0, 9});
            tiles.push_back({31, 160, 9});
        }
    } else if (level == 7) {
        constexpr int xs[5] = {0, 64, 128, 192, 256};
        if (screen < 10 || screen > 12) {
            if (extend_top_backdrop)
                for (int tx : xs) tiles.push_back({19, tx, -54});
            for (int ty : {9, 72, 135})
                for (int tx : xs) tiles.push_back({19, tx, ty});
            if (extend_top_backdrop &&
                screen >= 0 &&
                screen < static_cast<int>(screens.size())) {
                int ceiling = 0;
                for (const auto& tp : screens[static_cast<std::size_t>(
                         screen)].tiles)
                    if (tp.sprite_idx == 7 && tp.y == 9) ++ceiling;
                if (ceiling >= 3) {
                    for (int tx = 0; tx < 320; tx += 48)
                        tiles.push_back({4, tx, -21});
                    int cmin = 1 << 28;
                    for (const auto& tp : screens[static_cast<
                             std::size_t>(screen)].tiles)
                        if (tp.y == 9 &&
                            (tp.sprite_idx == 7 || tp.sprite_idx == 6))
                            cmin = std::min(cmin, tp.x);
                    if (cmin != (1 << 28) && cmin > 0)
                        for (int x = cmin - 48;;) {
                            if (x < 0) x = 0;
                            tiles.push_back({7, x, 9});
                            if (x == 0) break;
                            x -= 48;
                        }
                }
            }
        } else {
            for (int tx : xs) {
                tiles.push_back({29, tx, 79});
                tiles.push_back({31, tx, 40});
            }
        }
    }
    g.backdrop_tile_count = static_cast<int>(tiles.size());
    if (tile_screen >= 0 &&
        tile_screen < static_cast<int>(screens.size())) {
        for (const auto& tp :
             screens[static_cast<std::size_t>(tile_screen)].tiles) {
            const int idx = golden_resolve_sprite_idx(level, tp.sprite_idx);
            if (idx < 0) continue;
            tiles.push_back({idx, tp.x, tp.y});
        }
    }
    if (level == 7 && extend_top_backdrop)
        olduvai::presentation::l7_bridge_ceiling_to_wall(level_tiles,
                                                         tile_screen, tiles);
    if (glider_water_y >= 0 && screen >= 9 &&
        screen <= olduvai::core::kLastScreen)
        olduvai::presentation::normalize_glider_water(tiles, glider_water_y);
    if (extend_top_backdrop && !visual_background)
        olduvai::presentation::tile_patterns::extend_columns_to_top(
            tiles, tile_sprites);
    return g;
}

// ── Per-level asset config (mirrors game_app kLevels) ──────────────────────

struct TestLevel {
    int internal_id;
    bool visual_background;
    const char* tile_mats[2];
    bool grot3;   // append GROT3.MAT (L3 trunk body/cap)
};

constexpr TestLevel kTestLevels[] = {
    {1, true,  {"ELEML1.MAT", nullptr},       false},
    {3, false, {"ELEML3.MAT", "ELEML3B.MAT"}, true},
    {5, true,  {"ELEML5.MAT", "GROT5.MAT"},   false},
    {7, false, {"ELEML7.MAT", nullptr},       false},
};

int g_checked = 0;

#define REQUIRE(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "FAIL: %s  (line %d)\n", #cond, __LINE__); \
            return 1; \
        } \
    } while (0)

}  // namespace

int main() {
    fs::path dir;
    if (const char* env = std::getenv("OLDUVAI_GAME_DATA")) {
        dir = env;
    } else {
        dir = fs::path(OLDUVAI_SOURCE_DIR) / "game_data";
    }
    if (!fs::exists(dir / "FILESA.CUR") || !fs::exists(dir / "HISTORIK.EXE")) {
        std::fprintf(stderr,
                     "screen_tiles: SKIP — game data not found at %s\n",
                     dir.string().c_str());
        return 77;   // CTest SKIP_RETURN_CODE
    }

    const auto exe = slurp(dir / "HISTORIK.EXE");
    CurArchive fa(slurp(dir / "FILESA.CUR"));
    CurArchive fb(slurp(dir / "FILESB.CUR"));
    CurArchive va(slurp(dir / "FILESA.VGA"));
    CurArchive vb(slurp(dir / "FILESB.VGA"));
    auto entry_data = [&](const std::string& name)
        -> const std::vector<std::uint8_t>* {
        for (CurArchive* ar : {&fa, &fb, &va, &vb}) {
            if (ar->contains(name)) return &ar->get(name).data;
        }
        return nullptr;
    };

    for (const auto& lv : kTestLevels) {
        // Atlas: surface tile MATs in order, then GROT3 for L3 — exactly the
        // g.render.tile_sprites / ra.tile_sprites both engine paths feed.
        std::vector<Sprite> surface;
        for (const char* mat : lv.tile_mats) {
            if (mat == nullptr) continue;
            const auto* d = entry_data(mat);
            REQUIRE(d != nullptr);
            const auto sprites = MatFile(*d, mat).sprites();
            surface.insert(surface.end(), sprites.begin(), sprites.end());
        }
        const int surface_count = static_cast<int>(surface.size());
        std::vector<Sprite> atlas = surface;
        if (lv.grot3) {
            const auto* g3 = entry_data("GROT3.MAT");
            REQUIRE(g3 != nullptr);
            const auto sprites = MatFile(*g3, "GROT3.MAT").sprites();
            atlas.insert(atlas.end(), sprites.begin(), sprites.end());
        }
        const auto level_tiles =
            olduvai::prepare::read_tile_table(exe, lv.internal_id);
        REQUIRE(!level_tiles.screens.empty());

        olduvai::presentation::ScreenTileContext ctx;
        ctx.level = lv.internal_id;
        ctx.visual_background = lv.visual_background;
        ctx.surface_tile_count = surface_count;
        ctx.level_tiles = &level_tiles;
        ctx.tile_sprites = &atlas;

        const int n_screens = static_cast<int>(level_tiles.screens.size());
        for (int screen = 0; screen < n_screens; ++screen) {
            for (const bool extend : {false, true}) {
                // Glider-water baseline: engine uses it for the icy level
                // (L5) only; exercise disabled everywhere + a representative
                // enabled baseline on L5 so the normalisation path is under
                // the identity check too.
                std::vector<int> glider = {-1};
                if (lv.internal_id == 5) glider.push_back(150);
                for (const int gw : glider) {
                    ctx.extend_top_backdrop = extend;
                    ctx.glider_water_y = gw;

                    std::vector<TileDraw> got;
                    const int got_backdrop =
                        olduvai::presentation::build_screen_tiles(ctx, screen,
                                                                  got);
                    // Determinism / purity: a second run is bit-identical.
                    std::vector<TileDraw> again;
                    const int again_backdrop =
                        olduvai::presentation::build_screen_tiles(ctx, screen,
                                                                  again);
                    REQUIRE(again_backdrop == got_backdrop);
                    REQUIRE(again.size() == got.size());

                    const Golden want = golden_build(
                        level_tiles, atlas, lv.internal_id, screen,
                        surface_count, extend, lv.visual_background, gw);

                    if (want.tiles.size() != got.size() ||
                        want.backdrop_tile_count != got_backdrop) {
                        std::fprintf(
                            stderr,
                            "FAIL: L%d S%d extend=%d gw=%d — size %zu/%zu "
                            "backdrop %d/%d\n",
                            lv.internal_id, screen, extend ? 1 : 0, gw,
                            got.size(), want.tiles.size(), got_backdrop,
                            want.backdrop_tile_count);
                        return 1;
                    }
                    for (std::size_t i = 0; i < got.size(); ++i) {
                        const auto& a = got[i];
                        const auto& b = want.tiles[i];
                        if (a.sprite_idx != b.sprite_idx || a.x != b.x ||
                            a.y != b.y || again[i].sprite_idx != a.sprite_idx ||
                            again[i].x != a.x || again[i].y != a.y) {
                            std::fprintf(
                                stderr,
                                "FAIL: L%d S%d extend=%d gw=%d tile %zu — "
                                "got {%d,%d,%d} want {%d,%d,%d}\n",
                                lv.internal_id, screen, extend ? 1 : 0, gw, i,
                                a.sprite_idx, a.x, a.y, b.sprite_idx, b.x,
                                b.y);
                            return 1;
                        }
                    }
                    ++g_checked;
                }
            }
        }
        std::printf("screen_tiles: L%d — %d screens verified\n",
                    lv.internal_id, n_screens);
    }
    std::printf(
        "screen_tiles: OK — %d (level, screen, extend, glider) tile lists "
        "identical to the pre-refactor construction\n",
        g_checked);
    return 0;
}
