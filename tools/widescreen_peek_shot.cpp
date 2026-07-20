// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen adjacent-screen peek — headless composite-shot diagnostic.
//
// Kept tool (productionized from the original L1-only spike): renders a
// flip-screen surface screen S centered, with the coherent horizontal
// neighbors peeked into the side margins (background + terrain + the store's
// static objects + still-alive enemies at their spawn posts — §8.7 shipped
// 2026-07-04).  Neighbor
// resolution goes through presentation::widescreen_neighbors and the margin
// assembly through presentation::compose_widescreen, so the harness exercises
// the exact pure logic the real pipeline (Task 2) will use.  Use it for visual
// regression of the peek/bezel decision + seam coherence per level / margin.
//
//   widescreen_peek_shot <game_dir> <internal_level> <screen> <margin_px>
//   -> /tmp/wpeek_L<lvl>_s<NN>_m<MM>.png  (nearest-upscaled x4)
//
// Supports the shared-backdrop surface levels (internal 1 & 5 — the v1 peek
// set).  Other levels resolve to bezel margins (still composed, for the bezel
// look).  Asset cfg mirrors tools/screen_atlas.cpp.
//
// SDL_MAIN_HANDLED: on Windows SDL.h redefines main to SDL_main and expects
// the SDL2main WinMain shim; this console tool owns its own main instead.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "core/rng.hpp"
#include "formats/cur.hpp"
#include "prepare/exe_tables.hpp"
#include "presentation/game_render.hpp"
#include "presentation/image_out.hpp"
#include "presentation/widescreen.hpp"
#include "systems/spawning.hpp"

using namespace olduvai;

static std::vector<std::uint8_t> slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "args: <dir> <internal_level> <screen> [margin]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const int level = std::atoi(argv[2]);
    const int screen = std::atoi(argv[3]);
    const int margin = argc > 4 ? std::atoi(argv[4]) : 18;

    const auto exe = slurp(dir + "/HISTORIK.EXE");
    formats::CurArchive fa(slurp(dir + "/FILESA.CUR"));
    formats::CurArchive fbb(slurp(dir + "/FILESB.CUR"));
    formats::CurArchive va(slurp(dir + "/FILESA.VGA"));
    formats::CurArchive vb(slurp(dir + "/FILESB.VGA"));
    auto entry = [&](const std::string& n) -> const std::vector<std::uint8_t>* {
        for (formats::CurArchive* ar : {&fa, &fbb, &va, &vb})
            if (ar->contains(n)) return &ar->get(n).data;
        return nullptr;
    };

    // Asset cfg — mirrors tools/screen_atlas.cpp (levels 1/3/5/7).
    struct Cfg { const char* bg; bool vis; const char* m1; const char* m2;
                 const char* spr; std::uint32_t obj; };
    Cfg cfg = level == 3 ? Cfg{nullptr, false, "ELEML3.MAT", "ELEML3B.MAT", "L3SPR.MAT", 0x47EE}
            : level == 5 ? Cfg{"FOND5.PC1", true, "ELEML5.MAT", "GROT5.MAT", "L5SPR.MAT", 0x5840}
            : level == 7 ? Cfg{"FOND7.PC1", false, "ELEML7.MAT", nullptr, "L7SPR.MAT", 0x74FC}
                         : Cfg{"FOND1.PC1", true, "ELEML1.MAT", nullptr, "L1SPR.MAT", 0x33C2};

    presentation::LevelRenderAssets ra;
    if (cfg.bg) {
        ra.background = formats::parse_pc1(*entry(cfg.bg));
        ra.palette = ra.background.palette;
    }
    ra.visual_background = cfg.vis;
    if (level == 7) ra.palette = formats::parse_pc1(*entry("FOND7.PC1")).palette;
    ra.tile_sprites = formats::MatFile(*entry(cfg.m1), cfg.m1).sprites();
    if (cfg.m2) {
        auto s2v = formats::MatFile(*entry(cfg.m2), cfg.m2).sprites();
        ra.tile_sprites.insert(ra.tile_sprites.end(), s2v.begin(), s2v.end());
    }
    ra.entity_sprites = formats::MatFile(*entry(cfg.spr), cfg.spr).sprites();

    const auto tiles = prepare::read_tile_table(exe, level);
    const auto objs = prepare::read_object_table(exe, cfg.obj);
    const auto mt = systems::MonsterTables::from_exe(exe);
    const int surface_count = (int)tiles.screens.size();

    // Resolve which neighbors are coherent for this state (no peek → -1).
    const auto neigh = presentation::widescreen_neighbors(
        level, screen, /*secret_flag=*/false, surface_count);

    auto compose_screen = [&](int s, bool with_entities) {
        presentation::FrameBuffer fb;
        core::global_rng().reseed(1);
        systems::SystemsState st;
        st.current_level = level;
        ra.tiles.clear();
        if (s >= 0 && s < surface_count)
            for (const auto& tp : tiles.screens[(std::size_t)s].tiles)
                ra.tiles.push_back({tp.sprite_idx, tp.x, tp.y});
        if (with_entities && s >= 0 && s < (int)objs.size()) {
            st.entities = systems::spawn_screen_entities(objs[(std::size_t)s], mt);
            for (auto& e : st.entities) if (e.sprite < 0) e.sprite = 0;
        }
        st.player.sprite = -1;
        presentation::compose_frame(fb, st, ra);
        return fb;
    };

    // Center with entities; neighbors are background + terrain ONLY.
    const auto center = compose_screen(screen, /*with_entities=*/true);
    presentation::FrameBuffer left_fb, right_fb;
    const presentation::FrameBuffer* left = nullptr;
    const presentation::FrameBuffer* right = nullptr;
    if (neigh.left >= 0) { left_fb = compose_screen(neigh.left, false); left = &left_fb; }
    if (neigh.right >= 0) { right_fb = compose_screen(neigh.right, false); right = &right_fb; }

    // Pure-FOND backdrop (320×200 RGBA) for the no-neighbour margin extension —
    // the same FOND the center uses, converted to RGBA (no foreground tiles).
    // Surface (visual) levels only; matches the real pipeline's ws_backdrop.
    presentation::FrameBuffer backdrop_fb;
    const presentation::FrameBuffer* backdrop = nullptr;
    if (ra.visual_background && ra.background.width == 320 &&
        ra.background.pixels.size() >= 320u * 200u) {
        for (std::size_t i = 0; i < 320u * 200u; ++i) {
            const std::uint8_t idx = ra.background.pixels[i];
            const formats::Rgb c = (idx < ra.background.palette.size())
                                       ? ra.background.palette[idx]
                                       : formats::Rgb{};
            backdrop_fb.px[i * 4] = c.r; backdrop_fb.px[i * 4 + 1] = c.g;
            backdrop_fb.px[i * 4 + 2] = c.b; backdrop_fb.px[i * 4 + 3] = 255;
        }
        backdrop = &backdrop_fb;
    }

    std::vector<std::uint8_t> wide;
    presentation::compose_widescreen(wide, margin, center, left, right,
                                     /*hud_rows=*/0, backdrop);
    const int W = 320 + 2 * margin, H = 200;

    // Nearest-upscale x4 for a clear view.
    const int K = 4, UW = W * K, UH = H * K;
    std::vector<std::uint8_t> up((std::size_t)UW * UH * 4);
    for (int y = 0; y < UH; ++y)
        for (int x = 0; x < UW; ++x) {
            const std::uint8_t* s = &wide[((std::size_t)(y/K) * W + (x/K)) * 4];
            std::uint8_t* d = &up[((std::size_t)y * UW + x) * 4];
            d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=255;
        }

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        up.data(), UW, UH, 32, UW * 4, SDL_PIXELFORMAT_RGBA32);
    char name[96];
    std::snprintf(name, sizeof name, "/tmp/wpeek_L%d_s%02d_m%02d.png",
                  level, screen, margin);
    presentation::save_surface_image(surf, name);
    SDL_FreeSurface(surf);
    std::printf("wrote %s  (%dx%d native, margin %d => %d wide; "
                "neighbors L=%d R=%d)\n",
                name, W, H, margin, W, neigh.left, neigh.right);
    return 0;
}
