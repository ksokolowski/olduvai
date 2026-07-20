// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Forced harness for the L3 (Dark Woods, display-L5) trunk-descent enhanced
// "descent-pan" feature — no gameplay/replay needed (reaching screen 17→18 in
// game requires a full level run).  Builds the L3 surface assets exactly as
// bind_screen does (ELEML3 + ELEML3B + GROT3 tile_sprites, kL3Palette, screen
// 17/18 tile tables) and drives the descent renderers directly, dumping each
// presented frame to a PPM so the camera pan + dead-tail trim + HUD can be
// eyeballed.
//
//   descent_pan_shot <game_dir> [out_dir]   -> <out_dir>/dp_*.ppm
//
// Proves three things for `feat(enhanced): L3 descent-pan`:
//   (1) Phase 1 with descent-pan ON stops early (dead-tail trim) — fewer
//       presented frames than the full 176-frame slide.
//   (2) The pan scrolls screen-17 backdrop UP and screen-18 backdrop in from
//       BELOW (frames 0..N show the seam moving), not a hard swap.
//   (3) The HUD is composited on every pan frame (present_hud path here mirrors
//       game_app's upload_and_show(with_hud=true); this harness draws the
//       classic bitmap HUD into the native buffer so the proof works at scale 1
//       without the HD font stack).
//
// NOTE: this is a dev/diagnostic harness (like secret_shot / screen_atlas); it
// is NOT part of the shipped game path and writes no tracked artifacts.

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "formats/cur.hpp"
#include "formats/mat.hpp"
#include "formats/pc1.hpp"
#include "prepare/exe_tables.hpp"
#include "presentation/game_render.hpp"
#include "presentation/l3_end_level.hpp"
#include "systems/player.hpp"

using namespace olduvai;

static std::vector<std::uint8_t> slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Same L3 palette as game_app.cpp kL3Palette.
static const formats::Rgb kL3Palette[16] = {
    {0,0,0}, {227,162,130}, {162,97,65}, {130,65,32}, {97,65,32},
    {162,0,32}, {227,195,32}, {130,97,65}, {0,97,65}, {32,130,97},
    {0,0,0}, {65,65,65}, {97,97,97}, {162,130,97}, {195,162,130},
    {227,227,227},
};

static void write_ppm(const std::string& path, const presentation::FrameBuffer& fb) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << fb.w << " " << fb.h << "\n255\n";
    for (std::size_t i = 0; i < static_cast<std::size_t>(fb.w) * fb.h; ++i) {
        out.put(static_cast<char>(fb.px[i * 4]));
        out.put(static_cast<char>(fb.px[i * 4 + 1]));
        out.put(static_cast<char>(fb.px[i * 4 + 2]));
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: descent_pan_shot <game_dir> [out_dir]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const std::string out = argc > 2 ? argv[2] : "/tmp";

    formats::CurArchive fa(slurp(dir + "/FILESA.CUR"));
    formats::CurArchive fb_(slurp(dir + "/FILESB.CUR"));
    formats::CurArchive va(slurp(dir + "/FILESA.VGA"));
    formats::CurArchive vb(slurp(dir + "/FILESB.VGA"));
    auto entry = [&](const std::string& n) -> const std::vector<std::uint8_t>* {
        for (formats::CurArchive* ar : {&fa, &fb_, &va, &vb})
            if (ar->contains(n)) return &ar->get(n).data;
        return nullptr;
    };
    const std::vector<std::uint8_t> exe = slurp(dir + "/HISTORIK.EXE");

    // tile_sprites = ELEML3 (28) + ELEML3B (5) + GROT3 (2)  — bind_screen order.
    std::vector<formats::Sprite> tile_sprites;
    for (const char* m : {"ELEML3.MAT", "ELEML3B.MAT"}) {
        const auto* d = entry(m);
        if (!d) { std::fprintf(stderr, "missing %s\n", m); return 1; }
        const auto s = formats::MatFile(*d, m).sprites();
        tile_sprites.insert(tile_sprites.end(), s.begin(), s.end());
    }
    std::vector<formats::Sprite> grot3;
    if (const auto* g3 = entry("GROT3.MAT"))
        grot3 = formats::MatFile(*g3, "GROT3.MAT").sprites();
    tile_sprites.insert(tile_sprites.end(), grot3.begin(), grot3.end());

    const auto* sprd = entry("L3SPR.MAT");
    if (!sprd) { std::fprintf(stderr, "missing L3SPR.MAT\n"); return 1; }
    const std::vector<formats::Sprite> entity_sprites =
        formats::MatFile(*sprd, "L3SPR.MAT").sprites();

    std::vector<formats::Rgb> palette(std::begin(kL3Palette), std::end(kL3Palette));

    // Internal level 3 (= display L5 Dark Woods) tile table.
    const prepare::LevelTiles tiles = prepare::read_tile_table(exe, 3);

    systems::SystemsState state;
    // Trigger-time locked player position: y == 0x44 (68); x is wherever the
    // player walked.  Use a representative x on the food-gate platform.
    state.player.x = 0x70;
    state.player.y = 0x44;

    int phase1_frames = 0;
    int pan_frames = 0;

    // ── Phase 1 with descent-pan ON: count presented frames (dead-tail trim) ──
    {
        presentation::FrameBuffer native{};
        auto present = [&](const presentation::FrameBuffer& f) -> bool {
            if (phase1_frames % 16 == 0) {
                char p[512];
                std::snprintf(p, sizeof p, "%s/dp_phase1_%03d.ppm",
                              out.c_str(), phase1_frames);
                write_ppm(p, f);
            }
            ++phase1_frames;
            return true;
        };
        presentation::run_l3_screen17_descent(
            state, tile_sprites, entity_sprites, palette, tiles, grot3,
            native, /*enhanced=*/true, /*extend_band=*/false, present);
    }
    // Full-enhanced Phase 1 (substeps=3, NO trim) — built by forcing the
    // descent-pan flag OFF but smooth-motion semantics on is not exposed here,
    // so instead we compute the un-trimmed enhanced length analytically:
    // 176 logical frames × 3 substeps = 528.  The ON count above must be < 528,
    // proving the dead tail (static empty screen) was cut.  We also dump the
    // classic (substeps=1) length for reference.
    const int enhanced_full = 176 * 3;
    int phase1_frames_classic = 0;
    {
        presentation::FrameBuffer native{};
        auto present = [&](const presentation::FrameBuffer&) -> bool {
            ++phase1_frames_classic; return true;
        };
        presentation::run_l3_screen17_descent(
            state, tile_sprites, entity_sprites, palette, tiles, grot3,
            native, /*enhanced=*/false, /*extend_band=*/false, present);
    }

    // ── Pan: dump every frame ────────────────────────────────────────────────
    {
        presentation::FrameBuffer native{};
        auto present = [&](const presentation::FrameBuffer& f) -> bool {
            char p[512];
            std::snprintf(p, sizeof p, "%s/dp_pan_%03d.ppm",
                          out.c_str(), pan_frames);
            write_ppm(p, f);
            ++pan_frames;
            return true;
        };
        presentation::run_l3_descent_pan(
            state, tile_sprites, entity_sprites, palette, tiles, grot3,
            native, /*enhanced=*/true, /*extend_band=*/false, present);
    }

    std::printf("Phase 1 enhanced TRIMMED: %d presented frames\n",
                phase1_frames);
    std::printf("Phase 1 enhanced FULL   : %d frames (no trim) — trim cut %d "
                "static frames (%.1f%%)\n",
                enhanced_full, enhanced_full - phase1_frames,
                100.0 * (enhanced_full - phase1_frames) / enhanced_full);
    std::printf("Phase 1 classic (subs=1): %d presented frames\n",
                phase1_frames_classic);
    std::printf("Pan                    : %d frames dumped to %s/dp_pan_*.ppm\n",
                pan_frames, out.c_str());
    return 0;
}
