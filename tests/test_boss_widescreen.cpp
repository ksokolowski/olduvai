// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure (no-SDL) tests for boss_widescreen helpers (§boss-widescreen-margins)
// and compose_widescreen reflect_pure invariants (Task 6).
// Compiles boss_widescreen.cpp + widescreen.cpp directly; links SDL-free.

#include "enhance/hd_text.hpp"
#include "formats/mat.hpp"
#include "formats/pc1.hpp"
#include "presentation/render/boss_hud.hpp"
#include "presentation/render/boss_widescreen.hpp"
#include "presentation/render/widescreen.hpp"

#include <algorithm>
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
                           /*left=*/nullptr, /*right=*/nullptr, MarginFill{/*hud_rows=*/0, /*backdrop=*/nullptr,
                           /*reflect_pure=*/true});

        const int W = 320 + 2 * M;
        auto px = [&](int x, int y) -> const std::uint8_t* {
            return &wide[(static_cast<std::size_t>(y) * W + x) * 4];
        };
        CHECK(px(0,     100)[0] == 120);  // left margin mirrors grey (not black)
        CHECK(px(W - 1, 100)[0] == 120);  // right margin mirrors grey
        CHECK(px(M + 10, 100)[0] == 120); // center verbatim
    }

    // capture_boss_hud_bar (§3.18): captures the baked gradient BEFORE erasing
    // the HUD strip, and the erase must reach the green bar columns that
    // make_clean_boss_bg leaves behind (they are not bright).
    {
        std::vector<std::uint8_t> bg(320 * 200 * 4, 0);
        auto set = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            auto* p = &bg[(y * 320 + x) * 4]; p[0]=r; p[1]=g; p[2]=b; p[3]=255;
        };
        // Bar starts at x=280: green in rows 0-5 from there to kBossHealthStart.
        // Everything left of 280 in row 0 stays black so the scan has to walk.
        for (int y = 0; y < 6; ++y)
            for (int x = 280; x <= 317; ++x) set(x, y, 0, 97 + y, 32);
        // Donor rows (y + 9) under the bar columns — a colour nothing else uses.
        for (int y = 0; y < 6; ++y)
            for (int x = 280; x <= 317; ++x) set(x, y + 9, 11, 22, 33);

        const auto bar = capture_boss_hud_bar(bg);
        CHECK(bar.ok());
        CHECK(bar.left == 280);                 // scan found the first green col
        CHECK(bar.strip_w == 317 - 280 + 1);    // left..kBossHealthStart (317)
        // The captured strip holds the ORIGINAL gradient, row by row.
        for (int y = 0; y < 6; ++y) {
            const auto* s = &bar.strip[(static_cast<std::size_t>(y) * bar.strip_w) * 4];
            CHECK(s[1] == 97 + y);
        }
        // ...and bg no longer does: the bar columns are inpainted from the donor.
        auto px = [&](int x, int y) { return &bg[(y * 320 + x) * 4]; };
        CHECK(px(280, 0)[0] == 11);
        CHECK(px(317, 5)[2] == 33);
    }

    // A buffer that is not 320x200 RGBA is left completely alone — the size
    // check used to guard capture AND erase together, and still must.
    {
        std::vector<std::uint8_t> small(64 * 64 * 4, 7);
        const auto copy = small;
        const auto bar = capture_boss_hud_bar(small);
        CHECK(!bar.ok());
        CHECK(small == copy);
    }

    // BossHud::draw_into (§3.18): the whole HUD driven by TWO integers.
    //
    // No font is loaded here, and that is the point twice over: HdText::draw
    // no-ops without one, so what runs is exactly the energy gauge — and the
    // gauge is the part with the widescreen coordinate mapping in it.  The
    // test binds `lives` and `health` as the driver does and then moves only
    // `health`, which is the design claim being checked: nothing else about
    // the fight reaches the HUD.
    {
        // A bar baked at columns 280..317, rows 0-5, with a per-row green.
        std::vector<std::uint8_t> bg(320 * 200 * 4, 0);
        for (int y = 0; y < 6; ++y)
            for (int x = 280; x <= 317; ++x) {
                auto* p = &bg[(y * 320 + x) * 4];
                p[0] = 0; p[1] = 97 + y; p[2] = 32; p[3] = 255;
            }
        auto bar = capture_boss_hud_bar(bg);
        CHECK(bar.ok());

        olduvai::enhance::HdText no_font;   // ok() == false; draw() no-ops
        int lives = 3, health = 317;        // kBossHealthStart — full bar
        BossHud hud(&no_font, bar, &lives, &health);

        // 1:1 domain: bw == total_native_w == 320, bh == 200, so sx = sy = 1
        // and native coordinates land on themselves.
        std::vector<std::uint8_t> buf(320 * 200 * 4, 0);
        auto px = [&](std::vector<std::uint8_t>& b, int w, int x, int y) {
            return &b[(static_cast<std::size_t>(y) * w + x) * 4];
        };

        hud.draw_into(buf, 320, 200, /*draw_lives=*/true, 0, 320);
        // Row 3 samples strip row 2 → green 99.  Full health fills to 317.
        CHECK(px(buf, 320, 285, 3)[1] == 99);
        CHECK(px(buf, 320, 316, 3)[1] == 99);
        // White 1px frame at the bar's left edge (bar.left - 1) and row 0.
        CHECK(px(buf, 320, bar.left - 1, 3)[0] == 235);
        CHECK(px(buf, 320, 300, 0)[0] == 235);

        // Drain it: health below bar.left leaves the dark interior everywhere.
        std::fill(buf.begin(), buf.end(), 0);
        health = 279;
        hud.draw_into(buf, 320, 200, /*draw_lives=*/true, 0, 320);
        CHECK(px(buf, 320, 285, 3)[0] == 18);
        CHECK(px(buf, 320, 285, 3)[2] == 30);

        // Widescreen domain: centre origin M, total native width 320+2M.  With
        // bw == that width, sx is still 1, so the whole gauge shifts right by
        // exactly M — the mapping the wide HUD depends on.
        const int M = 40, WW = 320 + 2 * M;
        std::vector<std::uint8_t> wbuf(static_cast<std::size_t>(WW) * 200 * 4, 0);
        health = 317;
        hud.draw_into(wbuf, WW, 200, /*draw_lives=*/true, M, WW);
        CHECK(px(wbuf, WW, M + 285, 3)[1] == 99);   // gradient, shifted by M
        CHECK(px(wbuf, WW, 285, 3)[1] == 0);        // nothing at the unshifted x
    }

    // BossHud::draw_classic_lives — the CLASSIC (non-HD) stack.
    //
    // This test exists because moving the bitmap path onto BossHud introduced
    // a silent regression: the driver never called set_classic_font, so
    // draw_classic_lives returned early and the lives digits vanished in
    // classic mode. It built clean and every boss gate stayed green, because
    // ALL of them run --enhanced. Nothing covered this path at all.
    {
        // 8x6 solid Mono1bpp glyph, repeated across the charset. draw_text
        // indexes it as (char - 0x20), so '0' is 16 and any digit resolves.
        olduvai::formats::Sprite glyph;
        glyph.width = 8;
        glyph.height = 6;
        glyph.format = olduvai::formats::SpriteFormat::Mono1bpp;
        glyph.raw_pixels.assign(6, 0xFF);
        const std::vector<olduvai::formats::Sprite> charset(64, glyph);
        std::vector<olduvai::formats::Rgb> pal(16);
        pal[15] = {200, 100, 50};

        olduvai::enhance::HdText no_font;
        int lives = 3, health = 317;
        BossHud hud(&no_font, BossHudBar{}, &lives, &health);

        // Unbound font: draws nothing. This is the state the bug shipped in.
        FrameBuffer fb;
        hud.draw_classic_lives(fb);
        bool any = false;
        for (std::size_t i = 0; i < fb.px.size(); i += 4)
            if (fb.px[i] != 0) any = true;
        CHECK(!any);

        // Bound: "03" lands at (48,8) baseline, so rows 2..7, cols 48..63.
        hud.set_classic_font(&charset, &pal);
        hud.draw_classic_lives(fb);
        auto fpx = [&](int x, int y) { return &fb.px[(y * 320 + x) * 4]; };
        CHECK(fpx(48, 4)[0] == 200);   // first digit
        CHECK(fpx(56, 4)[1] == 100);   // second digit, proportional advance
        CHECK(fpx(48, 4)[2] == 50);
        CHECK(fpx(40, 4)[0] == 0);     // nothing left of the field
    }

    if (fails == 0) std::puts("boss_widescreen: OK");
    return fails == 0 ? 0 : 1;
}
