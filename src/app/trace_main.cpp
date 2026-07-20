// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// olduvai_trace — the native side of the cross-engine trace harness.
// Builds L1 screen 0 entirely from the user's game files, runs N frames of
// the canonical scripted input sequence, prints one trace line per frame:
//   frame px py sprite gravity club energy lives food score fireball
//   death rng_state climbing entity_count
// The comparison driver lives on the oracle side (private reference repo);
// 2026-06-10: 300/300 frames identical on first full run.
#include <cstdio>
#include <fstream>
#include "core/game_tables.hpp"
#include "core/rng.hpp"
#include "prepare/exe_tables.hpp"
#include "prepare/game_files.hpp"
#include "formats/cur.hpp"
#include "systems/frame_runner.hpp"
#include "systems/spawning.hpp"
using namespace olduvai;
static std::vector<std::uint8_t> slurp(const char* p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    // Accept a GOG install root (data/PREH layout) like the main binary.
    const std::string dir =
        prepare::resolve_game_dir(argv[1]).string();
    const int frames = std::atoi(argv[2]);
    const auto exe = prepare::load_game_executable(dir);
    // Runtime gameplay tables (cave widths, secret scores) come from the
    // user's executable, same as the main binary.
    core::GameTables gtables;
    gtables.cave_sizes = prepare::read_cave_size_table(exe);
    gtables.secret_scores = prepare::read_secret_score_table(exe);
    core::install_game_tables(gtables);

    // Collision: L1 screen 0 tiles stamped with LEVEL1.DUR.
    formats::CurArchive fa(slurp((dir + "/FILESA.CUR").c_str()));
    const auto dur = formats::parse_dur(fa.get("LEVEL1.DUR").data);
    const auto tiles = prepare::read_tile_table(exe, 1);
    systems::SystemsState st;
    for (const auto& tp : tiles.screens[0].tiles) {
        if (tp.sprite_idx >= 0 &&
            tp.sprite_idx < static_cast<int>(dur.tiles.size())) {
            st.collision.stamp_tile(
                dur.tiles[static_cast<std::size_t>(tp.sprite_idx)].segments,
                tp.x, tp.y);
        }
    }

    // Entities: L1 screen 0 from the object table; LCG seeded 1.
    core::global_rng().reseed(1);
    const auto mt = systems::MonsterTables::from_exe(exe);
    const auto screens = prepare::read_object_table(exe, 0x33C2);
    st.entities = systems::spawn_screen_entities(screens[0], mt);

    st.player.x = 100; st.player.y = 120;
    st.player.restart_x = 100; st.player.restart_y = 120;
    st.player.hit_counter = 0;
    st.current_level = 1; st.current_screen = 0;
    st.timer = 99;

    for (int f = 0; f < frames; ++f) {
        systems::FrameInputs in;
        if (f >= 10 && f < 60) in.right = true;
        if (f == 30 || f == 80) in.up = true;
        if (f >= 60 && f < 90) in.right = true;
        if (f == 95 || f == 130) in.attack = true;
        if (f >= 100 && f < 140) in.right = true;
        if (f >= 150 && f < 190) in.left = true;
        if (f >= 200 && f < 240) in.right = true;
        if (f == 222) in.down = true;
        if (f >= 250 && f < 280) { in.right = true; if (f % 9 == 0) in.up = true; }
        systems::run_frame(st, in);
        std::printf("%d %d %d %d %d %d %d %d %d %ld %d %d %u %d %d\n",
                    st.frame_counter, st.player.x, st.player.y,
                    st.player.sprite, st.player.gravity_flag,
                    st.player.club_flag, st.player.energy, st.player.lives,
                    st.food_count, st.score, st.fireball_flag,
                    st.player.death_counter, core::global_rng().state(),
                    st.player.climbing,
                    static_cast<int>(st.entities.size()));
    }
    return 0;
}
