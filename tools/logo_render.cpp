// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Render the project logo assets (assets/logo/*) from the SAME code the main
// menu uses for its title: HdText (Freckle Face, OFL) + the caveman
// fire-and-blood banner shade + the vector-bone silhouette geometry from
// menu_render's selection pointer.  Offline dev tool — outputs are committed;
// re-run after changing the title styling to keep the logo in sync.
//
//   usage: olduvai_logo [out_dir]      (default "assets/logo"; run from the
//                                       repo root so the font resolves)
//
// Transparency: HdText's blender writes hard alpha against the existing
// background, so everything is drawn twice — over black and over white — and
// the true anti-aliased alpha is recovered by difference matting
// (a = 255 - (white - black); colour = black * 255 / a).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "enhance/hd_text.hpp"
#include "presentation/banner_fx.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

namespace {

using olduvai::enhance::HdText;

// Colours shared with the icon (sampled from assets/icon/icon_src.png) and
// the menu title outline (menu_render.cpp).
constexpr std::uint8_t kBoneR = 238, kBoneG = 224, kBoneB = 192;
constexpr std::uint8_t kOutR = 18, kOutG = 6, kOutB = 6;
// Fixed animation phase for the caveman shade — the menu animates it; the
// logo freezes one good-looking frame (mid ember-breath).
constexpr float kShadePhase = 0.30f;

struct Canvas {
    int w = 0, h = 0;
    std::vector<std::uint8_t> black;   // rendered over opaque black
    std::vector<std::uint8_t> white;   // rendered over opaque white
    Canvas(int cw, int ch) : w(cw), h(ch) {
        black.assign(static_cast<std::size_t>(w) * h * 4, 0);
        white.assign(static_cast<std::size_t>(w) * h * 4, 255);
        for (std::size_t i = 3; i < black.size(); i += 4) black[i] = 255;
    }
};

// Blend one pixel the same way HdText does (src-over at coverage a).
void blend(std::vector<std::uint8_t>& rgba, int w, int x, int y, float cov,
           std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (cov <= 0.0f) return;
    const int a = static_cast<int>(std::min(1.0f, cov) * 255.0f + 0.5f);
    const std::size_t o = (static_cast<std::size_t>(y) * w + x) * 4;
    rgba[o] = static_cast<std::uint8_t>((r * a + rgba[o] * (255 - a)) / 255);
    rgba[o + 1] =
        static_cast<std::uint8_t>((g * a + rgba[o + 1] * (255 - a)) / 255);
    rgba[o + 2] =
        static_cast<std::uint8_t>((b * a + rgba[o + 2] * (255 - a)) / 255);
    rgba[o + 3] = 255;
}

// The menu's vector-bone silhouette (menu_render.cpp draw_vector_bone
// geometry: shaft capsule + four end knobs), rendered with an outline band —
// SDF gives smooth anti-aliased edges at any size.
void draw_bone(Canvas& c, float cx, float cy, float h) {
    const float w = h * 1.9f;              // score-bone aspect (25:13)
    const float x0 = cx - w / 2, x1 = cx + w / 2;
    const float r = h * 0.30f;             // end-knob radius
    const float rb = h * 0.20f;            // shaft half-thickness
    const float ky = h * 0.20f;            // knob vertical offset
    const float exl = x0 + r, exr = x1 - r;
    const float ow = std::max(2.0f, h * 0.055f);   // outline band width
    auto sdf = [&](float px, float py) {
        const float cxc = std::min(std::max(px, exl), exr);
        float d = std::hypot(px - cxc, py - cy) - rb;
        for (const float kx : {exl, exr})
            for (const float sy : {-1.0f, 1.0f})
                d = std::min(d, std::hypot(px - kx, py - (cy + sy * ky)) - r);
        return d;
    };
    const int ylo = static_cast<int>(cy - h), yhi = static_cast<int>(cy + h);
    const int xlo = static_cast<int>(x0 - ow - 2), xhi = static_cast<int>(x1 + ow + 2);
    for (int y = std::max(0, ylo); y < std::min(c.h, yhi); ++y) {
        for (int x = std::max(0, xlo); x < std::min(c.w, xhi); ++x) {
            const float d = sdf(static_cast<float>(x) + 0.5f,
                                static_cast<float>(y) + 0.5f);
            // coverage of the outlined shape (edge at d = ow)
            const float shape = std::clamp(0.5f - (d - ow), 0.0f, 1.0f);
            if (shape <= 0.0f) continue;
            // interior fill fraction (edge between fill and outline at d = 0)
            const float fill = std::clamp(0.5f - d, 0.0f, 1.0f);
            const float mix = fill;   // 1 = bone cream, 0 = charcoal outline
            const auto lerp = [&](std::uint8_t fc, std::uint8_t oc) {
                return static_cast<std::uint8_t>(fc * mix + oc * (1 - mix) + 0.5f);
            };
            for (auto* buf : {&c.black, &c.white})
                blend(*buf, c.w, x, y, shape, lerp(kBoneR, kOutR),
                      lerp(kBoneG, kOutG), lerp(kBoneB, kOutB));
        }
    }
}

// The menu title recipe (menu_render.cpp "OLDUVAI" branch): 8-way charcoal
// outline, caveman fire-blood styled fill, faux bold — on both mattes.
void draw_title(Canvas& c, HdText& font, const std::string& text, int tx,
                int baseline) {
    const int o = std::max(2, font.cap_px() / 9);
    const int bold = std::max(1, font.cap_px() / 30);
    for (auto* buf : {&c.black, &c.white}) {
        for (int dy = -o; dy <= o; dy += o)
            for (int dx = -o; dx <= o; dx += o)
                if (dx || dy)
                    font.draw(*buf, c.w, c.h, tx + dx, baseline + dy, text,
                              kOutR, kOutG, kOutB);
        const auto shade =
            olduvai::presentation::make_banner_shade("caveman", kShadePhase);
        font.draw_styled(*buf, c.w, c.h, tx, baseline, text, shade);
        font.draw_styled(*buf, c.w, c.h, tx + bold, baseline, text, shade);
    }
}

// Difference matting: recover anti-aliased alpha + un-premultiplied colour
// from the black/white renders, then save.
bool save_matted(const Canvas& c, const std::string& path) {
    std::vector<std::uint8_t> out(c.black.size(), 0);
    for (std::size_t o = 0; o < out.size(); o += 4) {
        int a3 = 0;
        for (int k = 0; k < 3; ++k)
            a3 += 255 - (c.white[o + k] - c.black[o + k]);
        const int a = std::clamp(a3 / 3, 0, 255);
        out[o + 3] = static_cast<std::uint8_t>(a);
        if (a > 0)
            for (int k = 0; k < 3; ++k)
                out[o + k] = static_cast<std::uint8_t>(
                    std::min(255, c.black[o + k] * 255 / a));
    }
    return stbi_write_png(path.c_str(), c.w, c.h, 4, out.data(), c.w * 4) != 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string out_dir = argc > 1 ? argv[1] : "assets/logo";
    HdText font;
    if (!font.load("", 4)) {   // resolves assets/fonts/ from the repo root
        std::fprintf(stderr, "logo_render: FreckleFace-Regular.ttf not found "
                             "(run from the repo root)\n");
        return 1;
    }
    const std::string text = "OLDUVAI";
    const int cap = 400;
    font.set_cap_px(cap);
    const int o = std::max(2, cap / 9);
    const int margin = o * 2;
    const int tw = font.measure(text);

    // Wordmark: text only.
    {
        Canvas c(tw + 2 * margin + cap / 30, cap + 2 * margin);
        draw_title(c, font, text, margin, margin + cap);
        if (!save_matted(c, out_dir + "/olduvai-wordmark.png")) return 1;
    }
    // Bone: standalone transparent silhouette (square-ish canvas).
    {
        const float bh = 520.0f;
        Canvas c(static_cast<int>(bh * 1.9f) + 2 * margin,
                 static_cast<int>(bh) + 2 * margin);
        draw_bone(c, c.w / 2.0f, c.h / 2.0f, bh);
        if (!save_matted(c, out_dir + "/bone.png")) return 1;
    }
    // Full logo: wordmark with the bone centred underneath.
    {
        const float bh = cap * 0.55f;
        const int gap = cap / 5;
        Canvas c(tw + 2 * margin + cap / 30,
                 margin + cap + gap + static_cast<int>(bh) + margin);
        draw_title(c, font, text, margin, margin + cap);
        draw_bone(c, c.w / 2.0f,
                  margin + cap + gap + bh / 2.0f, bh);
        if (!save_matted(c, out_dir + "/olduvai-logo.png")) return 1;
    }
    std::printf("logo_render: wrote olduvai-logo.png, olduvai-wordmark.png, "
                "bone.png to %s\n", out_dir.c_str());
    return 0;
}
