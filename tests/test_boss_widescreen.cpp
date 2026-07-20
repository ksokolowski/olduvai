// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure (no-SDL) tests for boss_widescreen helpers (§boss-widescreen-margins)
// and compose_widescreen reflect_pure invariants (Task 6).
// Compiles boss_widescreen.cpp + widescreen.cpp directly; links SDL-free.

#include "presentation/boss_widescreen.hpp"
#include "presentation/widescreen.hpp"

#include <cstdio>

using namespace olduvai::presentation;

static int fails = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) { std::fprintf(stderr, "FAIL line %d: %s\n",         \
                                    __LINE__, #cond); ++fails; }          \
    } while (0)

int main() {
    // boss_ws_margin: 16:10 -> 0, env override, cap
    {
        CHECK(boss_ws_margin(2560, 1600, nullptr) == 0);     // 16:10 == native AR
        CHECK(boss_ws_margin(2560, 1080, nullptr) > 0);      // 21:9 has margin
        CHECK(boss_ws_margin(2560, 1080, nullptr) <= 120);   // capped
        CHECK(boss_ws_margin(100, 100, "73") == 73);         // env override wins
        CHECK(boss_ws_margin(100, 100, "999") == 120);       // env clamped
        CHECK(boss_ws_margin(100, 100, "-5") == 0);
    }

    // make_clean_boss_bg: erases bright HUD pixels, keeps real scene
    {
        std::vector<std::uint8_t> bg(320 * 200 * 4, 0);
        auto set = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            auto* p = &bg[(y * 320 + x) * 4]; p[0]=r; p[1]=g; p[2]=b; p[3]=255;
        };
        // A bright HUD pixel at (100,3); donor row 12 of that column is green.
        set(100, 3, 230, 230, 230);
        set(100, 12, 10, 200, 40);
        // A real dark-green scene pixel at (50,3) that must survive.
        set(50, 3, 0, 97, 32);
        auto clean = make_clean_boss_bg(bg, 320, 200, 9);
        auto px = [&](int x, int y) { return &clean[(y * 320 + x) * 4]; };
        CHECK(px(100, 3)[1] == 200);  // replaced by donor (green)
        CHECK(px(100, 3)[0] == 10);
        CHECK(px(50, 3)[1] == 97);    // real scene untouched
    }

    // compose_widescreen reflect_pure: margins mirror the center, center verbatim.
    // Boss arenas use reflect_pure=true so legitimately black cave walls are NOT
    // void-scanned (the void scan replaces black with an interior colour, smearing
    // the cave wall).  With a grey fill the reflection must come back as grey.
    {
        FrameBuffer center;
        // Fill every channel with 120 and alpha 255 — a recognizable non-black
        // colour that survives the exact mirror check below.
        for (auto& v : center.px) v = 120;
        for (std::size_t i = 3; i < center.px.size(); i += 4) center.px[i] = 255;

        const int M = 73;
        std::vector<std::uint8_t> wide;
        compose_widescreen(wide, M, center,
                           /*left=*/nullptr, /*right=*/nullptr,
                           /*hud_rows=*/0, /*backdrop=*/nullptr,
                           /*reflect_pure=*/true);

        const int W = 320 + 2 * M;
        auto px = [&](int x, int y) -> const std::uint8_t* {
            return &wide[(static_cast<std::size_t>(y) * W + x) * 4];
        };
        CHECK(px(0,     100)[0] == 120);  // left margin mirrors grey (not black)
        CHECK(px(W - 1, 100)[0] == 120);  // right margin mirrors grey
        CHECK(px(M + 10, 100)[0] == 120); // center verbatim
    }

    if (fails == 0) std::puts("boss_widescreen: OK");
    return fails == 0 ? 0 : 1;
}
