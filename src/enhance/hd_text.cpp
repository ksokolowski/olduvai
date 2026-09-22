// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/hd_text.hpp"

#include "enhance/banner_shader.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
// Vendored single-header: silence its own -Wunused-function under -Werror.
#if defined(__GNUC__) || defined(__clang__)   // MSVC: C4068 unknown pragma
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)   // MSVC: C4068 unknown pragma
#pragma GCC diagnostic pop
#endif

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
    // Default active size: 8 px native x the HD compose scale.
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
        x += static_cast<float>(adv) * px_scale_;
        if (i + 1 < text.size()) {
            x += px_scale_ * static_cast<float>(stbtt_GetCodepointKernAdvance(
                                 info, text[i], text[i + 1]));
        }
    }
    return static_cast<int>(x + 0.5f);
}

const HdText::Glyph& HdText::glyph(int codepoint) const {
    std::uint32_t scale_bits = 0;
    std::memcpy(&scale_bits, &px_scale_, sizeof scale_bits);
    const std::uint64_t key =
        (static_cast<std::uint64_t>(scale_bits) << 32) |
        static_cast<std::uint32_t>(codepoint);
    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) return it->second;
    if (glyphs_.size() >= kMaxGlyphs) glyphs_.clear();
    Glyph g;
    const auto* info = reinterpret_cast<const stbtt_fontinfo*>(info_);
    unsigned char* bm = stbtt_GetCodepointBitmap(
        info, px_scale_, px_scale_, codepoint, &g.w, &g.h, &g.xoff, &g.yoff);
    if (bm != nullptr) {
        g.bitmap.assign(bm, bm + static_cast<std::size_t>(g.w) * g.h);
        stbtt_FreeBitmap(bm, nullptr);
    }
    return glyphs_.emplace(key, std::move(g)).first->second;
}

// Rasterise `text` and blend it into `rgba`, asking `color(dx, dy, r, g, b)`
// for the ink at each covered pixel.  See the declaration in hd_text.hpp for
// why this is a template.
template <typename ColorFn>
void HdText::rasterise(std::vector<std::uint8_t>& rgba, int buf_w, int buf_h,
                       int x, int baseline_y, const std::string& text,
                       ColorFn color) const {
    const auto* info = reinterpret_cast<const stbtt_fontinfo*>(info_);
    float pen = static_cast<float>(x);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int ch = static_cast<unsigned char>(text[i]);
        int w = 0, h = 0, xoff = 0, yoff = 0;
        const Glyph& gl = glyph(ch);
        w = gl.w;
        h = gl.h;
        xoff = gl.xoff;
        yoff = gl.yoff;
        const std::uint8_t* const bitmap =
            gl.bitmap.empty() ? nullptr : gl.bitmap.data();
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
                    std::uint8_t cr = 235, cg = 235, cb = 235;
                    color(dx, dy, cr, cg, cb);
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
        }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
        pen += static_cast<float>(adv) * px_scale_;
        if (i + 1 < text.size()) {
            pen += px_scale_ * static_cast<float>(
                                   stbtt_GetCodepointKernAdvance(info, ch,
                                                                 text[i + 1]));
        }
    }
}

void HdText::draw(std::vector<std::uint8_t>& rgba, int buf_w, int buf_h,
                  int x, int baseline_y, const std::string& text,
                  std::uint8_t cr, std::uint8_t cg, std::uint8_t cb) const {
    if (info_ == nullptr) return;
    rasterise(rgba, buf_w, buf_h, x, baseline_y, text,
              [cr, cg, cb](int, int, std::uint8_t& r, std::uint8_t& g,
                           std::uint8_t& b) { r = cr; g = cg; b = cb; });
}

void HdText::draw_banner(std::vector<std::uint8_t>& rgba, int buf_w,
                         int buf_h, int x, int baseline_y,
                         const std::string& text,
                         const BannerShader& shader) const {
    if (info_ == nullptr || buf_w <= 0) return;
    const float x0 = static_cast<float>(x);
    const float wspan = std::max(1.0f, static_cast<float>(measure(text)));
    const float ytop = static_cast<float>(baseline_y - cap_px_);
    const float hspan = std::max(1.0f, static_cast<float>(cap_px_));
    col_term_.resize(static_cast<std::size_t>(buf_w));
    col_done_.assign(static_cast<std::size_t>(buf_w), 0);
    rasterise(rgba, buf_w, buf_h, x, baseline_y, text,
              [&](int dx, int dy, std::uint8_t& r, std::uint8_t& g,
                  std::uint8_t& b) {
                  float u = (static_cast<float>(dx) - x0) / wspan;
                  float v = (static_cast<float>(dy) - ytop) / hspan;
                  u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
                  v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                  // u depends on dx alone, so the column term is too.
                  const auto c = static_cast<std::size_t>(dx);
                  if (col_done_[c] == 0) {
                      col_term_[c] = shader.column_term(u);
                      col_done_[c] = 1;
                  }
                  shader.shade(u, v, col_term_[c], r, g, b);
              });
}

}  // namespace olduvai::enhance
