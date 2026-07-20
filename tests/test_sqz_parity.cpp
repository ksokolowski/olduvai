// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SQZ executable parity gate — real game data (skip 77 when absent).
//
// When a game directory has BOTH the plain executable and the compressed
// PREH.SQZ container, every table the prepare pipeline reads must decode
// to identical values from either source.  This is the guarantee that GOG
// / CD copies play the exact same game as the canonical build: the CD
// re-link shifted data offsets (see detect_exe_layout), but the table
// CONTENT is unchanged, and this gate proves both readers land on it.
//
// Uses OLDUVAI_GAME_DATA (falls back like the other data-gated tests) and
// needs HISTORIK.EXE + PREH.SQZ side by side; skips otherwise.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include "formats/unsqz.hpp"
#include "prepare/exe_tables.hpp"

namespace fs = std::filesystem;
using namespace olduvai;

namespace {

std::vector<std::uint8_t> slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "sqz_parity: MISMATCH — %s\n", what);
        ++g_failures;
    }
}

bool same_tiles(const prepare::LevelTiles& a, const prepare::LevelTiles& b) {
    if (a.level != b.level || a.screens.size() != b.screens.size()) {
        return false;
    }
    for (std::size_t s = 0; s < a.screens.size(); ++s) {
        const auto& ta = a.screens[s].tiles;
        const auto& tb = b.screens[s].tiles;
        if (a.screens[s].screen != b.screens[s].screen) return false;
        if (ta.size() != tb.size()) return false;
        for (std::size_t i = 0; i < ta.size(); ++i) {
            if (ta[i].sprite_idx != tb[i].sprite_idx || ta[i].x != tb[i].x ||
                ta[i].y != tb[i].y || ta[i].layer != tb[i].layer) {
                return false;
            }
        }
    }
    return true;
}

bool same_objects(const prepare::ObjectScreens& a,
                  const prepare::ObjectScreens& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t s = 0; s < a.size(); ++s) {
        if (a[s].size() != b[s].size()) return false;
        for (std::size_t i = 0; i < a[s].size(); ++i) {
            if (a[s][i].type != b[s][i].type ||
                a[s][i].words != b[s][i].words) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main() {
    fs::path dir;
    if (const char* env = std::getenv("OLDUVAI_GAME_DATA")) {
        dir = env;
    } else {
        dir = fs::path(OLDUVAI_SOURCE_DIR) / "game_data";
    }
    const fs::path exe_path = dir / "HISTORIK.EXE";
    const fs::path sqz_path = dir / "PREH.SQZ";
    if (!fs::exists(exe_path) || !fs::exists(sqz_path)) {
        std::fprintf(stderr,
                     "sqz_parity: SKIP — need HISTORIK.EXE + PREH.SQZ in %s\n",
                     dir.string().c_str());
        return 77;   // CTest SKIP_RETURN_CODE
    }

    const auto exe = slurp(exe_path);
    const auto sqz_raw = slurp(sqz_path);
    if (!formats::looks_like_sqz(sqz_raw)) {
        std::fprintf(stderr, "sqz_parity: FAIL — PREH.SQZ not an SQZ file\n");
        return 1;
    }
    const auto unpacked = formats::unsqz(sqz_raw);

    // The decoded container must be a well-formed MZ executable and map to
    // a KNOWN layout (an unknown digest would silently read at delta 0).
    check(unpacked.size() >= 2 && unpacked[0] == 'M' && unpacked[1] == 'Z',
          "decoded SQZ is not an MZ executable");
    const prepare::ExeLayout layout = prepare::detect_exe_layout(unpacked);
    check(std::string(layout.variant) != "canonical",
          "decoded executable is not a recognised build variant");

    // Every table the engine reads, from both sources.
    for (const int level : {1, 3, 5, 7}) {
        check(same_tiles(prepare::read_tile_table(exe, level),
                         prepare::read_tile_table(unpacked, level)),
              "tile table");
    }
    for (const auto& t : prepare::object_tables()) {
        check(same_objects(prepare::read_object_table(exe, t.ds_offset),
                           prepare::read_object_table(unpacked, t.ds_offset)),
              t.name);
    }
    for (const auto& ref : prepare::monster_table_refs()) {
        const auto a = prepare::read_monster_table(exe, ref.ds_offset);
        const auto b = prepare::read_monster_table(unpacked, ref.ds_offset);
        check(a.score == b.score && a.energy == b.energy &&
                  a.ko_spr == b.ko_spr && a.init_spr == b.init_spr &&
                  a.move_spr == b.move_spr && a.away_spr == b.away_spr &&
                  a.walk_offsets == b.walk_offsets,
              "monster table");
        if (ref.alt_ds_offset != 0) {
            const auto aa = prepare::read_monster_table(exe, ref.alt_ds_offset);
            const auto bb =
                prepare::read_monster_table(unpacked, ref.alt_ds_offset);
            check(aa.score == bb.score && aa.walk_offsets == bb.walk_offsets,
                  "alt monster table");
        }
    }

    if (g_failures == 0) {
        std::printf("sqz_parity: OK — every prepared table identical "
                    "(variant: %s, ds_delta %+d)\n",
                    layout.variant, static_cast<int>(layout.ds_delta));
        return 0;
    }
    std::fprintf(stderr, "sqz_parity: %d mismatching tables\n", g_failures);
    return 1;
}
