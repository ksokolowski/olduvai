// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/boss_hud.hpp"

#include <algorithm>
#include <cstdio>

#include "enhance/hd_text.hpp"
#include "presentation/render/boss_widescreen.hpp"
#include "presentation/render/hud_render.hpp"
#include "systems/boss.hpp"

namespace olduvai::presentation {

BossHudBar capture_boss_hud_bar(std::vector<std::uint8_t>& bg) {
    using systems::kBossHealthStart;
    BossHudBar bar;
    if (bg.size() != 320u * 200u * 4u) return bar;

    // The bar's leftmost clearly green column in row 0 at x >= 268 (just after
    // the ENERGY label).
    for (int x = 268; x < 318; ++x) {
        const std::size_t off = (static_cast<std::size_t>(0) * 320 + x) * 4;
        const int r = bg[off], g = bg[off + 1], b = bg[off + 2];
        if (g > 60 && g > r && g > b) {
            bar.left = x;
            break;
        }
    }

    // Capture rows 0-5, columns left..kBossHealthStart, before the erase; the
    // overlay reproduces the baked gradient from it.
    bar.strip_w = std::max(0, kBossHealthStart - bar.left + 1);
    bar.strip.resize(static_cast<std::size_t>(6) * bar.strip_w * 4);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < bar.strip_w; ++x) {
            const int src_x = bar.left + x;
            const std::size_t si = (static_cast<std::size_t>(y) * 320 + src_x) * 4;
            const std::size_t di =
                (static_cast<std::size_t>(y) * bar.strip_w + x) * 4;
            bar.strip[di]     = bg[si];
            bar.strip[di + 1] = bg[si + 1];
            bar.strip[di + 2] = bg[si + 2];
            bar.strip[di + 3] = bg[si + 3];
        }
    }

    // make_clean_boss_bg removes the bright HUD pixels (the white text and
    // border); the green bar pixels are not bright, so inpaint those columns
    // from the donor row below the strip (y + kBossHudStrip) the same way.
    bg = make_clean_boss_bg(bg, 320, 200, kBossHudStrip);
    for (int y = 0; y < 6; ++y) {
        for (int x = bar.left; x <= kBossHealthStart; ++x) {
            const std::size_t o = (static_cast<std::size_t>(y) * 320 + x) * 4;
            const std::size_t s =
                (static_cast<std::size_t>(y + kBossHudStrip) * 320 + x) * 4;
            bg[o]     = bg[s];
            bg[o + 1] = bg[s + 1];
            bg[o + 2] = bg[s + 2];
            bg[o + 3] = 255;
        }
    }
    return bar;
}

void BossHud::draw_classic_lives(FrameBuffer& fb) const {
    if (charset_ == nullptr || palette_ == nullptr) return;
    // Clamped at both ends: %02d of an unbounded int can write 10 bytes into 8
    // (-Wformat-truncation).  Two digits by design (the EXE caps lives at 99).
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d", std::min(std::max(0, *lives_), 99));
    draw_text(fb, *charset_, *palette_, 48, 8, buf);
}

void BossHud::draw_into(const enhance::Canvas& cv,
                        bool draw_lives, int cx_native, int total_native_w) {
    using systems::kBossHealthStart;
    enhance::HdText& hd_text = *text_;

    const double sx = cv.w / static_cast<double>(total_native_w);
    const double sy = cv.h / 200.0;
    const int base_y = static_cast<int>(8 * sy + 0.5);

    // Pin the font cap to the centre-320 metric (8 native px x sx); the
    // overlay's default is sized by the full output width and too big in
    // widescreen. Restored at the end.
    const int saved_cap = hd_text.cap_px();
    hd_text.set_cap_px(std::max(1, static_cast<int>(8.0 * sx + 0.5)));

    // native x → output x in the wide domain: (cx_native + x) * sx.
    auto ox = [&](int nx) {
        return static_cast<int>((cx_native + nx) * sx + 0.5);
    };

    // LIVES as one string, so the proportional font never overprints the
    // digits.
    if (draw_lives) {
        char vbuf[16];
        // Same two-ended clamp as draw_classic_lives above, same reason.
        std::snprintf(vbuf, sizeof vbuf, "LIVES: %02d",
                      std::min(std::max(0, *lives_), 99));
        hd_text.draw(cv, ox(0), base_y, vbuf, {235, 235, 235});
    } else {
        hd_text.draw(cv, ox(0), base_y, "LIVES:", {235, 235, 235});
    }

    // Energy gauge in the surface gauge style (1 px white border, dark drained
    // interior) with the boss bar's own gradient inside; ENERGY right-aligned
    // just left of the frame.  Native bar extent: [bar_.left ..
    // kBossHealthStart] x rows 0-5; the box spans rows 0-7 with the gradient on
    // rows 1-6.  Colours as enhanced_hud (kWhite 235,235,235 / kEmpty
    // 18,18,30).
    auto fill_nat = [&](int nx0, int ny0, int nx1, int ny1,
                        int cr, int cg, int cb) {
        const int oxa = ox(nx0);
        const int oxb = ox(nx1 + 1);
        const int oya = static_cast<int>(ny0 * sy + 0.5);
        const int oyb = static_cast<int>((ny1 + 1) * sy + 0.5);
        for (int oy = oya; oy < oyb && oy < cv.h; ++oy)
            for (int oxx = oxa; oxx < oxb && oxx < cv.w; ++oxx) {
                const std::size_t oi =
                    (static_cast<std::size_t>(oy) * cv.w + oxx) * 4;
                cv.px[oi] = cr; cv.px[oi + 1] = cg; cv.px[oi + 2] = cb;
                cv.px[oi + 3] = 255;
            }
    };

    if (bar_.ok()) {
        const int health_col = std::min(*health_, kBossHealthStart);
        const int fx0 = bar_.left - 1, fx1 = kBossHealthStart + 1;
        const int fy0 = 0, fy1 = 7;

        // Dark drained interior across the whole inner area first.
        fill_nat(bar_.left, 1, kBossHealthStart, 6, 18, 18, 30);

        // Gradient fill [bar_.left..health_col], native rows 1-6 ← strip 0-5
        // (preserves the captured boss-energy colours).
        for (int nrow = 1; nrow <= 6; ++nrow) {
            const int srow = nrow - 1;
            const int oya = static_cast<int>(nrow * sy + 0.5);
            const int oyb = static_cast<int>((nrow + 1) * sy + 0.5);
            for (int oy = oya; oy < oyb && oy < cv.h; ++oy)
                for (int ncol = bar_.left; ncol <= health_col; ++ncol) {
                    const int scol =
                        std::min(ncol - bar_.left, bar_.strip_w - 1);
                    const std::size_t si =
                        (static_cast<std::size_t>(srow) * bar_.strip_w + scol) * 4;
                    const int oxa = ox(ncol), oxb = ox(ncol + 1);
                    for (int oxx = oxa; oxx < oxb && oxx < cv.w; ++oxx) {
                        const std::size_t oi =
                            (static_cast<std::size_t>(oy) * cv.w + oxx) * 4;
                        cv.px[oi] = bar_.strip[si];
                        cv.px[oi + 1] = bar_.strip[si + 1];
                        cv.px[oi + 2] = bar_.strip[si + 2];
                        cv.px[oi + 3] = 255;
                    }
                }
        }

        // White 1px frame (4 edges).
        fill_nat(fx0, fy0, fx1, fy0, 235, 235, 235);   // top
        fill_nat(fx0, fy1, fx1, fy1, 235, 235, 235);   // bottom
        fill_nat(fx0, fy0, fx0, fy1, 235, 235, 235);   // left
        fill_nat(fx1, fy0, fx1, fy1, 235, 235, 235);   // right
        // ENERGY label right-aligned just left of the frame.
        const int gap = static_cast<int>(4 * sx + 0.5);
        const int label_right = ox(fx0) - gap;
        const int label_w = hd_text.measure("ENERGY");
        hd_text.draw(cv, label_right - label_w, base_y, "ENERGY",
                     {235, 235, 235});
    } else {
        hd_text.draw(cv, ox(150), base_y, "ENERGY", {235, 235, 235});
    }
    hd_text.set_cap_px(saved_cap);   // restore for any later text in the pass
}

}  // namespace olduvai::presentation
