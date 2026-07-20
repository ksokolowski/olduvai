// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Compose the L1 SECRET room directly (floor + trampoline) and dump a PPM —
// no gameplay/replay needed.  For diagnosing the trampoline z-order + bubbles.
//   secret_shot <game_dir>  -> /tmp/secret_olduvai.ppm
#include <cstdio>
#include <fstream>
#include "enhance/hd_asset_cache.hpp"
#include "formats/cur.hpp"
#include "formats/mat.hpp"
#include "presentation/game_render.hpp"
#include "systems/fluid_bubbles.hpp"
#include "systems/player.hpp"
#include "systems/secret.hpp"
using namespace olduvai;
static std::vector<std::uint8_t> slurp(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main(int argc, char** argv) {
    const std::string dir = argv[1];
    formats::CurArchive fa(slurp(dir + "/FILESA.CUR"));
    formats::CurArchive va(slurp(dir + "/FILESA.VGA"));
    auto entry = [&](const std::string& n) -> const std::vector<std::uint8_t>* {
        for (formats::CurArchive* ar : {&fa, &va})
            if (ar->contains(n)) return &ar->get(n).data;
        return nullptr;
    };
    presentation::LevelRenderAssets ra;
    ra.visual_background = false;
    ra.bg_fill_index = 4;
    const formats::Rgb sec[16] = {
        {0,0,0},{108,216,216},{108,180,216},{72,144,216},{72,180,252},
        {108,144,252},{0,216,216},{0,0,0},{0,108,144},{0,144,180},
        {0,0,0},{36,108,108},{72,144,144},{108,180,180},{144,216,216},
        {108,252,252}};
    for (auto& c : sec) ra.palette.push_back(c);
    ra.tile_sprites = formats::MatFile(*entry("ELEML1.MAT"), "ELEML1.MAT").sprites();
    ra.entity_sprites = formats::MatFile(*entry("L1SPR.MAT"), "L1SPR.MAT").sprites();
    // Secret floor every 48 px at y=168 (matches refresh_secret_tiles).
    for (int si = 0; si < 320; si += 0x30)
        ra.tiles.push_back({1, si, systems::kSecretFloorY});
    systems::SystemsState st;
    st.secret_flag = 1;
    st.player.sprite = -1;
    const int scale = argc > 2 ? std::atoi(argv[2]) : 1;   // 1 or 4 (HD omniscale)
    enhance::HdAssetCache cache;
    std::string prof = "omniscale";
    presentation::FrameBuffer fb(320 * scale, 200 * scale);
    presentation::RenderTarget rt{fb.px.data(), fb.w, fb.h, scale,
                                  scale > 1 ? &cache : nullptr,
                                  scale > 1 ? &prof : nullptr};
    // Enhanced fluid bubbles (the mode the owner runs): advance to a steady
    // state and draw them as a post-background hook (before the floor tiles,
    // matching game_app + Python).  No EXE scatter — fluid replaces it.
    systems::FluidBubbleSystem fbs;
    fbs.init();
    for (int i = 0; i < 40; ++i) fbs.tick();
    const auto& bubbles = fbs.bubbles();
    std::function<void(presentation::RenderTarget&)> hook =
        [&](presentation::RenderTarget& frame) {
            for (const auto& b : bubbles) {
                const int idx = b.sprite_idx;
                if (idx >= 0 && idx < (int)ra.tile_sprites.size())
                    presentation::blit_sprite_keyed(
                        frame, ra.tile_sprites[(std::size_t)idx], ra.palette,
                        (int)b.x, (int)b.y);
            }
        };
    presentation::compose_frame(rt, st, ra, /*draw_player=*/true, hook);
    std::ofstream out("/tmp/secret_olduvai.ppm", std::ios::binary);
    out << "P6\n" << fb.w << " " << fb.h << "\n255\n";
    for (std::size_t i = 0; i < static_cast<std::size_t>(fb.w) * fb.h; ++i) {
        out.put((char)fb.px[i * 4]); out.put((char)fb.px[i * 4 + 1]);
        out.put((char)fb.px[i * 4 + 2]);
    }
    std::printf("wrote /tmp/secret_olduvai.ppm (%dx%d)\n", fb.w, fb.h);
    return 0;
}
