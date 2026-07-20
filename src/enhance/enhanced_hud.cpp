// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/enhanced_hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace olduvai::enhance {

namespace {

struct Rgb8 { int r, g, b; };
constexpr Rgb8 kRed{210, 55, 45};
constexpr Rgb8 kYellow{235, 200, 45};
constexpr Rgb8 kGreen{80, 210, 80};
constexpr Rgb8 kWhite{235, 235, 235};
constexpr Rgb8 kEmpty{18, 18, 30};

Rgb8 lerp(const Rgb8& a, const Rgb8& b, double t) {
    return {static_cast<int>(a.r + (b.r - a.r) * t + 0.5),
            static_cast<int>(a.g + (b.g - a.g) * t + 0.5),
            static_cast<int>(a.b + (b.b - a.b) * t + 0.5)};
}

// Status colour by fill fraction: red -> yellow -> green.
Rgb8 grad(double f) {
    f = std::clamp(f, 0.0, 1.0);
    return f < 0.5 ? lerp(kRed, kYellow, f / 0.5)
                   : lerp(kYellow, kGreen, (f - 0.5) / 0.5);
}

void fill_rect(std::vector<std::uint8_t>& px, int bw, int bh, int x, int y,
               int w, int h, const Rgb8& c) {
    for (int yy = y; yy < y + h; ++yy) {
        if (yy < 0 || yy >= bh) continue;
        for (int xx = x; xx < x + w; ++xx) {
            if (xx < 0 || xx >= bw) continue;
            const std::size_t o = (static_cast<std::size_t>(yy) * bw + xx) * 4;
            px[o] = static_cast<std::uint8_t>(c.r);
            px[o + 1] = static_cast<std::uint8_t>(c.g);
            px[o + 2] = static_cast<std::uint8_t>(c.b);
            px[o + 3] = 255;
        }
    }
}

}  // namespace

EnhancedHudLayout compute_enhanced_hud_layout(
    HdText& text, const systems::SystemsState& state) {
    EnhancedHudLayout L;
    // Measure glyph widths in NATIVE pixels (8 px cap height = the design
    // metric); every coordinate below is native, scaled per-pass by callers.
    text.set_cap_px(8);
    char buf[16];

    // Vertical rhythm (native coords).
    constexpr int r1 = 10;
    constexpr int r2 = 21;
    constexpr int bar_y = 13;
    constexpr int bar_w = 41, bar_h = 8;
    constexpr int label_gap = 6, col_gap = 18;

    auto add_text = [&](int x, int y, const std::string& str, const Rgb8& c) {
        L.texts.push_back({x, y, str, static_cast<std::uint8_t>(c.r),
                           static_cast<std::uint8_t>(c.g),
                           static_cast<std::uint8_t>(c.b)});
    };
    // 1px (native) closed white border box with the dark empty fill.
    auto add_box = [&](int x, int y, int w, int h) {
        L.boxes.push_back({x, y, w, h});
    };

    // ── Column A: Score over Food ──
    add_text(0, r1, "Score", kWhite);
    add_text(0, r2, "Food", kWhite);
    const int a_v =
        std::max(text.measure("Score"), text.measure("Food")) + label_gap;
    std::snprintf(buf, sizeof buf, "%06ld", std::min(state.score, 999999L));
    add_text(a_v, r1, buf, kWhite);
    const int score_w = text.measure(buf);
    add_box(a_v, bar_y, bar_w, bar_h);
    const int food = std::min(state.food_count, 46);
    const int food_fill = static_cast<int>(std::lround(39.0 * food / 46.0));
    if (food_fill > 0) {
        const Rgb8 c = grad(food / 46.0);
        L.fills.push_back({a_v + 1, bar_y + 1, food_fill, bar_h - 2,
                           c.r, c.g, c.b});
    }
    const int a_r = a_v + std::max(score_w, bar_w);

    // ── Column B: Lives over Energy ──
    const int b_x = a_r + col_gap;
    add_text(b_x, r1, "Lives", kWhite);
    add_text(b_x, r2, "Energy", kWhite);
    const int b_v =
        b_x + std::max(text.measure("Lives"), text.measure("Energy")) +
        label_gap;
    std::snprintf(buf, sizeof buf, "%02d", std::max(0, state.player.lives));
    add_text(b_v, r1, buf, kWhite);
    add_box(b_v, bar_y, bar_w, bar_h);
    const int energy = std::clamp(state.player.energy, 0, 10);
    const Rgb8 pip = grad(energy / 10.0);
    for (int i = 0; i < energy; ++i) {   // ten pips, edge-to-edge tiling
        L.fills.push_back({b_v + 1 + i * 4, bar_y + 1, 3, bar_h - 2,
                           pip.r, pip.g, pip.b});
    }

    // ── Column C: Time right-aligned to x=318 ──
    std::snprintf(buf, sizeof buf, "%02d", std::max(0, state.timer));
    const int time_w = text.measure(buf);
    const Rgb8 time_c = state.timer < 11 ? kRed : kWhite;
    add_text(318 - time_w, r1, buf, time_c);
    add_text(318 - time_w - label_gap - text.measure("Time"), r1, "Time",
             kWhite);
    return L;
}

void draw_enhanced_hud_bars(std::vector<std::uint8_t>& px, int bw, int bh,
                            int s, const EnhancedHudLayout& L, int x_off_native) {
    // Gauge outline + dark empty fill, native coords × s (x shifted by the
    // native x-origin so the bars land at the centre in a wide buffer).
    for (const auto& b : L.boxes) {
        const int x = (b.x + x_off_native) * s, y = b.y * s,
                  w = b.w * s, h = b.h * s;
        fill_rect(px, bw, bh, x, y, w, s, kWhite);             // top
        fill_rect(px, bw, bh, x, y + h - s, w, s, kWhite);     // bottom
        fill_rect(px, bw, bh, x, y, s, h, kWhite);             // left
        fill_rect(px, bw, bh, x + w - s, y, s, h, kWhite);     // right
        fill_rect(px, bw, bh, x + s, y + s, w - 2 * s, h - 2 * s, kEmpty);
    }
    // Food fill / energy pips.
    for (const auto& f : L.fills) {
        fill_rect(px, bw, bh, (f.x + x_off_native) * s, f.y * s, f.w * s,
                  f.h * s, {f.r, f.g, f.b});
    }
}

void draw_enhanced_hud_text(std::vector<std::uint8_t>& px, int ow, int oh,
                            const HdText& text, const EnhancedHudLayout& L) {
    // Native x scales by ow/320, native baseline by oh/200.  The font is
    // already sized to the output cap height by the caller.
    const double sx = ow / 320.0;
    const double sy = oh / 200.0;
    for (const auto& t : L.texts) {
        const int x = static_cast<int>(t.x * sx + 0.5);
        const int y = static_cast<int>(t.baseline_y * sy + 0.5);
        text.draw(px, ow, oh, x, y, t.str, t.r, t.g, t.b);
    }
}

}  // namespace olduvai::enhance
