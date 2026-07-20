// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/hd_text.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
// Vendored single-header: silence its own -Wunused-function under -Werror.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

namespace olduvai::enhance {

namespace {
constexpr int kTargetCapPx = 8;   // native cap height (the locked metric)
}

void HdText::report_missing(const std::string& exe_dir,
                            const std::string& font_file) {
    std::fprintf(stderr,
        "hd-text: font \"%s\" not found - vector text/HUD disabled for this "
        "run.\n"
        "         Searched: %s/fonts/, $OLDUVAI_FONT, and the source-tree "
        "fallbacks.\n"
        "         The font ships in the release's \"fonts\" folder next to "
        "the executable;\n"
        "         restore it, or download the face from Google Fonts "
        "(OFL-licensed)\n"
        "         and place it at fonts/%s.\n",
        font_file.c_str(), exe_dir.c_str(), font_file.c_str());
}

bool HdText::load(const std::string& exe_dir, int scale,
                  const std::string& font_file) {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("OLDUVAI_FONT")) {
        candidates.push_back(env);
    }
    candidates.push_back(exe_dir + "/fonts/" + font_file);
    candidates.push_back(exe_dir + "/../assets/fonts/" + font_file);
    candidates.push_back("assets/fonts/" + font_file);
    for (const auto& path : candidates) {
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        font_data_.assign(std::istreambuf_iterator<char>(in),
                          std::istreambuf_iterator<char>());
        if (!font_data_.empty()) break;
    }
    if (font_data_.empty()) return false;

    info_storage_.resize(sizeof(stbtt_fontinfo));
    auto* info = reinterpret_cast<stbtt_fontinfo*>(info_storage_.data());
    if (stbtt_InitFont(info, font_data_.data(),
                       stbtt_GetFontOffsetForIndex(font_data_.data(), 0)) ==
        0) {
        font_data_.clear();
        return false;
    }
    info_ = info;
    // Probe the cap height of "S" at a fixed 100 px so any target cap height
    // can be derived later (set_cap_px) without re-initialising the font.
    const float probe = stbtt_ScaleForPixelHeight(info, 100.0f);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(info, 'S', probe, probe, &x0, &y0, &x1, &y1);
    const float cap_at_100 = static_cast<float>(y1 - y0);
    probe_scale_ = probe;
    probe_cap_px_ = cap_at_100 > 0 ? cap_at_100 : 70.0f;
    // Default active size: 8 px native × the HD compose scale.
    set_cap_px(kTargetCapPx * scale);
    return true;
}

void HdText::set_cap_px(int cap_px) {
    if (probe_cap_px_ <= 0) return;
    cap_px_ = cap_px;
    px_scale_ = probe_scale_ * static_cast<float>(cap_px) / probe_cap_px_;
}

int HdText::measure(const std::string& text) const {
    if (info_ == nullptr) return 0;
    const auto* info = reinterpret_cast<const stbtt_fontinfo*>(info_);
    float x = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, text[i], &adv, &lsb);
        x += adv * px_scale_;
        if (i + 1 < text.size()) {
            x += px_scale_ * stbtt_GetCodepointKernAdvance(info, text[i],
                                                           text[i + 1]);
        }
    }
    return static_cast<int>(x + 0.5f);
}

void HdText::draw(std::vector<std::uint8_t>& rgba, int buf_w, int buf_h,
                  int x, int baseline_y, const std::string& text,
                  std::uint8_t cr, std::uint8_t cg, std::uint8_t cb) const {
    if (info_ == nullptr) return;
    const auto* info = reinterpret_cast<const stbtt_fontinfo*>(info_);
    float pen = static_cast<float>(x);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int ch = text[i];
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(
            info, px_scale_, px_scale_, ch, &w, &h, &xoff, &yoff);
        if (bitmap != nullptr) {
            const int gx = static_cast<int>(pen + 0.5f) + xoff;
            const int gy = baseline_y + yoff;
            for (int yy = 0; yy < h; ++yy) {
                const int dy = gy + yy;
                if (dy < 0 || dy >= buf_h) continue;
                for (int xx = 0; xx < w; ++xx) {
                    const int dx = gx + xx;
                    if (dx < 0 || dx >= buf_w) continue;
                    const int a = bitmap[yy * w + xx];
                    if (a == 0) continue;
                    const std::size_t o =
                        (static_cast<std::size_t>(dy) * buf_w + dx) * 4;
                    rgba[o] = static_cast<std::uint8_t>(
                        (cr * a + rgba[o] * (255 - a)) / 255);
                    rgba[o + 1] = static_cast<std::uint8_t>(
                        (cg * a + rgba[o + 1] * (255 - a)) / 255);
                    rgba[o + 2] = static_cast<std::uint8_t>(
                        (cb * a + rgba[o + 2] * (255 - a)) / 255);
                    rgba[o + 3] = 255;
                }
            }
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
        pen += adv * px_scale_;
        if (i + 1 < text.size()) {
            pen += px_scale_ * stbtt_GetCodepointKernAdvance(info, ch,
                                                             text[i + 1]);
        }
    }
}

void HdText::draw_styled(std::vector<std::uint8_t>& rgba, int buf_w, int buf_h,
                         int x, int baseline_y, const std::string& text,
                         const ShadeFn& shade) const {
    if (info_ == nullptr || !shade) return;
    const auto* info = reinterpret_cast<const stbtt_fontinfo*>(info_);
    // Text bbox for gradient normalisation: width = measure(); the glyph band
    // runs from the cap top (baseline - cap_px) down to the baseline.
    const float x0 = static_cast<float>(x);
    const float wspan = std::max(1.0f, static_cast<float>(measure(text)));
    const float ytop = static_cast<float>(baseline_y - cap_px_);
    const float hspan = std::max(1.0f, static_cast<float>(cap_px_));
    float pen = x0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int ch = text[i];
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(
            info, px_scale_, px_scale_, ch, &w, &h, &xoff, &yoff);
        if (bitmap != nullptr) {
            const int gx = static_cast<int>(pen + 0.5f) + xoff;
            const int gy = baseline_y + yoff;
            for (int yy = 0; yy < h; ++yy) {
                const int dy = gy + yy;
                if (dy < 0 || dy >= buf_h) continue;
                float v = (static_cast<float>(dy) - ytop) / hspan;
                v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                for (int xx = 0; xx < w; ++xx) {
                    const int dx = gx + xx;
                    if (dx < 0 || dx >= buf_w) continue;
                    const int a = bitmap[yy * w + xx];
                    if (a == 0) continue;
                    float u = (static_cast<float>(dx) - x0) / wspan;
                    u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
                    std::uint8_t cr = 235, cg = 235, cb = 235;
                    shade(u, v, cr, cg, cb);
                    const std::size_t o =
                        (static_cast<std::size_t>(dy) * buf_w + dx) * 4;
                    rgba[o] = static_cast<std::uint8_t>(
                        (cr * a + rgba[o] * (255 - a)) / 255);
                    rgba[o + 1] = static_cast<std::uint8_t>(
                        (cg * a + rgba[o + 1] * (255 - a)) / 255);
                    rgba[o + 2] = static_cast<std::uint8_t>(
                        (cb * a + rgba[o + 2] * (255 - a)) / 255);
                    rgba[o + 3] = 255;
                }
            }
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
        pen += adv * px_scale_;
        if (i + 1 < text.size()) {
            pen += px_scale_ * stbtt_GetCodepointKernAdvance(info, ch,
                                                             text[i + 1]);
        }
    }
}

}  // namespace olduvai::enhance
