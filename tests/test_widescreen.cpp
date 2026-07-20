// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure (no-SDL) decision-table test for widescreen_neighbors (§8.7).
// Compiles widescreen.cpp directly; FrameBuffer's transitive includes pull no
// SDL, so this links without SDL2.

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
    // Cave / secret screen (>=100) → bezel both sides, on a peek level.
    {
        auto n = widescreen_neighbors(1, 100, false, 10);
        CHECK(n.left == -1 && n.right == -1);
    }
    // secret_flag set also forces bezel even on a normal screen index.
    {
        auto n = widescreen_neighbors(1, 3, true, 10);
        CHECK(n.left == -1 && n.right == -1);
    }
    // Boss level (internal 2) → bezel.
    {
        auto n = widescreen_neighbors(2, 0, false, 1);
        CHECK(n.left == -1 && n.right == -1);
    }
    CHECK(widescreen_neighbors(4, 1, false, 5).left == -1);
    CHECK(widescreen_neighbors(4, 1, false, 5).right == -1);
    CHECK(widescreen_neighbors(6, 1, false, 5).left == -1);
    CHECK(widescreen_neighbors(6, 1, false, 5).right == -1);

    // Surface levels 3 (dark woods) + 7 (volcanic) now ALSO peek their real
    // adjacent screens (was bezel) — same neighbour-peek path as 1/5; only the
    // no-neighbour first/last edge differs (no FOND backdrop).
    {
        auto n3 = widescreen_neighbors(3, 3, false, 10);
        CHECK(n3.left == 2 && n3.right == 4);
        auto n7 = widescreen_neighbors(7, 3, false, 10);
        CHECK(n7.left == 2 && n7.right == 4);
    }

    // L7 (internal 7) lava cave-hall warp seams (screens 9|10 cave-descent and
    // 12|13 teleport) are non-contiguous → peek suppressed on each side, so the
    // cave side (10/12) reads dark and the surface side (9/13) extends its rock
    // wall.  Within-hall seams (10↔11, 11↔12) still peek.
    {
        // surface entry screen 9: right (→ cave) suppressed, left still peeks.
        auto n9 = widescreen_neighbors(7, 9, false, 20);
        CHECK(n9.left == 8 && n9.right == -1);
        // cave entry screen 10: left (→ surface 9) suppressed, right peeks 11.
        auto n10 = widescreen_neighbors(7, 10, false, 20);
        CHECK(n10.left == -1 && n10.right == 11);
        // cave middle screen 11: both within-hall peeks intact.
        auto n11 = widescreen_neighbors(7, 11, false, 20);
        CHECK(n11.left == 10 && n11.right == 12);
        // cave exit screen 12: left peeks 11, right (→ surface 13) suppressed.
        auto n12 = widescreen_neighbors(7, 12, false, 20);
        CHECK(n12.left == 11 && n12.right == -1);
        // surface exit screen 13: left (→ cave 12) suppressed, right peeks 14.
        auto n13 = widescreen_neighbors(7, 13, false, 20);
        CHECK(n13.left == -1 && n13.right == 14);
    }

    // L1 (internal 1) mid-screen: both neighbors peek.
    {
        auto n = widescreen_neighbors(1, 3, false, 10);
        CHECK(n.left == 2 && n.right == 4);
    }
    // L1 first screen: no left neighbor, right peeks.
    {
        auto n = widescreen_neighbors(1, 0, false, 10);
        CHECK(n.left == -1 && n.right == 1);
    }
    // L1 last screen: left peeks, no right neighbor.
    {
        const int count = 10;
        auto n = widescreen_neighbors(1, count - 1, false, count);
        CHECK(n.left == count - 2 && n.right == -1);
    }
    // L5 (internal 5) mid-screen also peeks.
    {
        auto n = widescreen_neighbors(5, 4, false, 12);
        CHECK(n.left == 3 && n.right == 5);
    }
    // L5 single-surface-screen level: both edges → bezel.
    {
        auto n = widescreen_neighbors(5, 0, false, 1);
        CHECK(n.left == -1 && n.right == -1);
    }

    // ── compose_widescreen: null-neighbor WITH backdrop = background WRAP ──────
    // The FOND backdrop tiles horizontally, so a no-neighbour margin WRAPS it
    // (right margin samples FOND's LEFT cols [r]; left margin samples FOND's
    // RIGHT cols [kW-margin+x]) — exactly like a real neighbour peek.  Upper
    // band = backdrop; bottom kWideGroundBandRows = CENTER (own floor wrapped).
    {
        constexpr int kW = 320, kH = 200, margin = 8;
        FrameBuffer center{kW, kH};
        FrameBuffer backdrop{kW, kH};
        auto put_col = [&](FrameBuffer& f, int x, std::uint8_t r,
                           std::uint8_t g, std::uint8_t b) {
            for (int y = 0; y < kH; ++y) {
                std::uint8_t* p =
                    &f.px[(static_cast<std::size_t>(y) * f.w + x) * 4];
                p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
            }
        };
        // Paint the WRAP source columns with a per-column gradient (G = the
        // source col offset).  Right margin wraps to LEFT cols [0..margin);
        // left margin wraps to RIGHT cols [kW-margin..kW).  R marks the layer:
        // backdrop 200(left-cols)/210(right-cols), center 30/40.
        for (int c = 0; c < margin; ++c) {
            put_col(backdrop, c, 200, static_cast<std::uint8_t>(c), 0);     // right-margin src
            put_col(backdrop, kW - margin + c, 210, static_cast<std::uint8_t>(c), 0); // left-margin src
            put_col(center, c, 30, static_cast<std::uint8_t>(c), 0);
            put_col(center, kW - margin + c, 40, static_cast<std::uint8_t>(c), 0);
        }
        put_col(center, 100, 77, 88, 99);   // interior marker (center verbatim)

        std::vector<std::uint8_t> out;
        compose_widescreen(out, margin, center, nullptr, nullptr,
                           /*hud_rows=*/0, &backdrop);
        const int W = kW + 2 * margin;
        CHECK(out.size() == static_cast<std::size_t>(W) * kH * 4);
        auto at = [&](int x, int y) -> const std::uint8_t* {
            return &out[(static_cast<std::size_t>(y) * W + x) * 4];
        };
        const int ground_top = kH - kWideGroundBandRows;
        bool upper_ok = true, ground_ok = true, no_bezel = true,
             center_ok = true;
        for (int y = 0; y < kH; ++y) {
            const bool ground = y >= ground_top;
            for (int x = 0; x < margin; ++x) {
                const std::uint8_t* l = at(x, y);              // left margin, wraps col kW-margin+x
                const std::uint8_t* r = at(margin + kW + x, y); // right margin, wraps col x
                if (ground) {
                    // Ground band MIRRORS the screen's near edge when non-void:
                    // left margin x → center col (margin-1-x) = painted
                    // (30, margin-1-x); right margin x → center col (kW-1-x) =
                    // painted (40, margin-1-x).  (Void near edges stay black —
                    // covered by the void test below.)
                    if (!(l[0] == 30 && l[1] == margin - 1 - x)) ground_ok = false;
                    if (!(r[0] == 40 && r[1] == margin - 1 - x)) ground_ok = false;
                } else {
                    if (!(l[0] == 210 && l[1] == x)) upper_ok = false;
                    if (!(r[0] == 200 && r[1] == x)) upper_ok = false;
                }
                if ((l[0] == 0 && l[1] == 0 && l[2] == 0) ||
                    (r[0] == 0 && r[1] == 0 && r[2] == 0))
                    no_bezel = false;
            }
            const std::uint8_t* c = at(margin + 100, y);
            if (!(c[0] == 77 && c[1] == 88 && c[2] == 99)) center_ok = false;
        }
        CHECK(upper_ok);
        CHECK(ground_ok);
        CHECK(no_bezel);
        CHECK(center_ok);
        // Upper-band wrap applies for ALL rows incl. the HUD strip: row 0,
        // left x=0 wraps backdrop col (kW-margin) → R=210, G=0.
        std::vector<std::uint8_t> out2;
        compose_widescreen(out2, margin, center, nullptr, nullptr,
                           /*hud_rows=*/32, &backdrop);
        const std::uint8_t* row0 = &out2[0];   // x=0, y=0 (upper band)
        CHECK(row0[0] == 210 && row0[1] == 0);  // wraps backdrop col kW-margin

        // Ground band whose near EDGE STRIP is empty (L1's black ground strip:
        // the solid ground starts under the player) must STAY black — the mirror
        // of a void region is void.  Repaint the whole near strip (cols
        // [0,margin) and [kW-margin,kW)) pure black (void) and re-compose.
        for (int y = 0; y < kH; ++y) {
            for (int c = 0; c < margin; ++c) {
                std::uint8_t* pl =
                    &center.px[(static_cast<std::size_t>(y) * kW + c) * 4];
                std::uint8_t* pr =
                    &center.px[(static_cast<std::size_t>(y) * kW + (kW - 1 - c)) *
                               4];
                pl[0] = pl[1] = pl[2] = 0; pr[0] = pr[1] = pr[2] = 0;
            }
        }
        std::vector<std::uint8_t> out3;
        compose_widescreen(out3, margin, center, nullptr, nullptr,
                           /*hud_rows=*/0, &backdrop);
        bool void_clamps_black = true;
        for (int y = ground_top; y < kH; ++y) {
            const std::uint8_t* l = &out3[(static_cast<std::size_t>(y) * W) * 4];
            const std::uint8_t* r =
                &out3[(static_cast<std::size_t>(y) * W + (margin + kW)) * 4];
            if (!(l[0] == 0 && l[1] == 0 && l[2] == 0)) void_clamps_black = false;
            if (!(r[0] == 0 && r[1] == 0 && r[2] == 0)) void_clamps_black = false;
        }
        CHECK(void_clamps_black);
    }

    // ── compose_widescreen: null-neighbor WITHOUT backdrop = legacy self-tile ─
    // Secret rooms pass a null backdrop and MUST keep the mirror/self-tile fill
    // (no bezel) — this branch must not regress.
    {
        constexpr int kW = 320, kH = 200, margin = 8;
        FrameBuffer center{kW, kH};
        auto put = [&](FrameBuffer& f, int x, std::uint8_t r,
                       std::uint8_t g, std::uint8_t b) {
            for (int y = 0; y < kH; ++y) {
                std::uint8_t* p =
                    &f.px[(static_cast<std::size_t>(y) * f.w + x) * 4];
                p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
            }
        };
        for (int c = 0; c < margin; ++c) {
            put(center, c, static_cast<std::uint8_t>(10 + c), 0, 0);
            put(center, kW - 1 - c, 0, static_cast<std::uint8_t>(10 + c), 0);
        }
        std::vector<std::uint8_t> out;
        compose_widescreen(out, margin, center, nullptr, nullptr,
                           /*hud_rows=*/0, /*backdrop=*/nullptr);
        const int W = kW + 2 * margin;
        auto at = [&](int x, int y) -> const std::uint8_t* {
            return &out[(static_cast<std::size_t>(y) * W + x) * 4];
        };
        bool left_ok = true, right_ok = true;
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < margin; ++x) {
                const std::uint8_t* l = at(x, y);
                if (l[0] != 10 + (margin - 1 - x)) left_ok = false;  // mirror
                const std::uint8_t* r = at(margin + kW + x, y);
                if (r[1] != 10 + x) right_ok = false;
            }
        }
        CHECK(left_ok);
        CHECK(right_ok);
    }

    // ── reflect_pure: black edge columns stay black (no void-scan) ───────────
    {
        FrameBuffer center; center.px.assign(320 * 200 * 4, 0);
        // Column 0 = black (void), column 1 = red, rest red.
        for (int y = 0; y < 200; ++y)
            for (int x = 1; x < 320; ++x) {
                auto* p = &center.px[(y * 320 + x) * 4];
                p[0] = 200; p[1] = 0; p[2] = 0; p[3] = 255;
            }
        // M=1: the single left-margin pixel mirrors column 0 (black).
        // mirror_col = M-1-x = 0-0 = 0 for x=0, so col 0 = black (void).
        const int M = 1;
        std::vector<std::uint8_t> def, pure;
        compose_widescreen(def,  M, center, nullptr, nullptr, 0, nullptr, false);
        compose_widescreen(pure, M, center, nullptr, nullptr, 0, nullptr, true);
        const int W = 320 + 2 * M;
        // Left-most margin pixel mirrors column 0 (black).
        auto px = [&](const std::vector<std::uint8_t>& b, int x, int y) {
            return &b[(y * W + x) * 4];
        };
        // Default (false): void-scan pulls the red from column 1.
        CHECK(px(def, 0, 100)[0] == 200);
        // reflect_pure (true): col 0 stays black — no void-scan.
        CHECK(px(pure, 0, 100)[0] == 0);
        CHECK(px(pure, 0, 100)[1] == 0);
    }

    if (fails == 0) std::puts("widescreen: OK");
    return fails == 0 ? 0 : 1;
}
