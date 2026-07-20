// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Screen atlas — renders every surface screen of a platform level
// (tiles + per-level backdrop + spawned entities, no player) for route
// planning when authoring replay scenarios.  Pair with the floor-segment
// dump (tile table x DUR shapes) for exact jump timing.
//
//   screen_atlas <game_dir> <internal_level> <first> <last>
//   -> /tmp/at_L<level>_sNN.ppm per screen
#include <cstdio>
#include <fstream>
#include "core/rng.hpp"
#include "formats/cur.hpp"
#include "formats/dur.hpp"
#include "prepare/exe_tables.hpp"
#include "presentation/game_render.hpp"
#include "systems/spawning.hpp"
using namespace olduvai;
static std::vector<std::uint8_t> slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main(int /*argc*/, char** argv) {
    const std::string dir = argv[1];
    const int level = std::atoi(argv[2]);          // internal id
    const int s0 = std::atoi(argv[3]), s1 = std::atoi(argv[4]);
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
    if (level == 3) {
        const std::uint8_t p3[16][3] = {
            {0,0,0},{227,162,130},{162,97,65},{130,65,32},{97,65,32},
            {162,0,32},{227,195,32},{130,97,65},{0,97,65},{32,130,97},
            {0,0,0},{65,65,65},{97,97,97},{162,130,97},{195,162,130},
            {227,227,227}};
        for (auto& c : p3) ra.palette.push_back({c[0], c[1], c[2]});
    } else if (level == 7) {
        ra.palette = formats::parse_pc1(*entry("FOND7.PC1")).palette;
    }
    ra.tile_sprites = formats::MatFile(*entry(cfg.m1), cfg.m1).sprites();
    if (cfg.m2) {
        auto s2v = formats::MatFile(*entry(cfg.m2), cfg.m2).sprites();
        ra.tile_sprites.insert(ra.tile_sprites.end(), s2v.begin(), s2v.end());
    }
    ra.entity_sprites = formats::MatFile(*entry(cfg.spr), cfg.spr).sprites();
    const auto tiles = prepare::read_tile_table(exe, level);
    const auto objs = prepare::read_object_table(exe, cfg.obj);
    const auto mt = systems::MonsterTables::from_exe(exe);
    core::global_rng().reseed(1);
    presentation::FrameBuffer fb;
    auto alias = [&](int idx) {
        if (level != 3) return idx;
        switch (idx) { case 29: return 30; case 28: return -1;
                       case 19: return 31; case 4: return 28; default: return idx; }
    };
    for (int s = s0; s <= s1 && s < (int)tiles.screens.size(); ++s) {
        systems::SystemsState st;
        st.current_level = level;
        ra.tiles.clear();
        if (level == 3 && s != 10 && s != 11) {
            ra.tiles.push_back({31, 0, 9});
            ra.tiles.push_back({31, 160, 9});
        } else if (level == 7) {
            const int xs[5] = {0, 64, 128, 192, 256};
            if (s < 10 || s > 12) {
                for (int ty : {9, 72, 135})
                    for (int tx : xs) ra.tiles.push_back({19, tx, ty});
            } else {
                for (int tx : xs) {
                    ra.tiles.push_back({29, tx, 79});
                    ra.tiles.push_back({31, tx, 40});
                }
            }
        }
        for (const auto& tp : tiles.screens[(std::size_t)s].tiles) {
            const int idx = alias(tp.sprite_idx);
            if (idx >= 0) ra.tiles.push_back({idx, tp.x, tp.y});
        }
        if (s < (int)objs.size())
            st.entities = systems::spawn_screen_entities(objs[(std::size_t)s], mt);
        for (auto& e : st.entities) if (e.sprite < 0) e.sprite = 0;
        st.player.sprite = -1;
        presentation::compose_frame(fb, st, ra);
        char name[64];
        std::snprintf(name, sizeof name, "/tmp/at_L%d_s%02d.ppm", level, s);
        std::ofstream out(name, std::ios::binary);
        out << "P6\n320 200\n255\n";
        for (std::size_t i = 0; i < 320 * 200; ++i) {
            out.put((char)fb.px[i*4]); out.put((char)fb.px[i*4+1]);
            out.put((char)fb.px[i*4+2]);
        }
    }
    return 0;
}
