// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// HD vector text — TrueType glyph rendering (stb_truetype) for the
// enhanced HUD.  The font auto-sizes so the capital height lands at
// 8 px native x target scale, matching the locked design's metric.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "enhance/canvas.hpp"

namespace olduvai::enhance {

class BannerShader;

class HdText {
public:
    // Load the bundled font (beside the executable, or OLDUVAI_FONT).  scale =
    // HD target scale (2/3/4); cap height 8*scale px.  `font_file` picks the
    // face (default FreckleFace; --hd-font noto = NotoSans-Regular.ttf).
    bool load(const std::string& exe_dir, int scale,
              const std::string& font_file = "FreckleFace-Regular.ttf");
    bool ok() const { return !font_data_.empty(); }

    // One-time stderr note when a vector-font feature could not load the font:
    // where it looked and where to get it (the release "fonts" folder, Google
    // Fonts, OFL).  ASCII only.
    static void report_missing(const std::string& exe_dir,
                               const std::string& font_file);

    // Size the em so a capital "S" is `cap_px` tall (the text overlay uses
    // 8*output_w/320).  measure()/draw() use it.
    void set_cap_px(int cap_px);

    // The active cap height, so a caller that resizes for one draw can restore
    // it.
    int cap_px() const { return cap_px_; }

    // Width of `text` in pixels at the active em.
    int measure(const std::string& text) const;

    // Draw with the baseline at HD (x, y); alpha-blended.
    void draw(const Canvas& cv, int x, int baseline_y,
              const std::string& text, formats::Rgb ink) const;

    // Banner effect (enhance/banner_shader.hpp): each covered pixel coloured by
    // the shader at (u, v) over the text box; the column term is computed once
    // per column.  Blending as in draw().
    void draw_banner(const Canvas& cv, int x, int baseline_y,
                     const std::string& text,
                     const BannerShader& shader) const;

private:
    // The glyph walk behind draw() and draw_banner(): rasterise each character,
    // blend its coverage into `cv` (clipped), advance by the advance width
    // plus kerning.  `color(dx, dy, ink)` supplies the ink per pixel.  A
    // template so draw()'s constant colour costs nothing; instantiated in
    // hd_text.cpp.
    template <typename ColorFn>
    void rasterise(const Canvas& cv, int x, int baseline_y,
                   const std::string& text, ColorFn color) const;

    // Rasterised glyphs keyed by (scale, codepoint): the overlay redraws
    // animated text every present, and stb rebuilds and mallocs a glyph on
    // every call. Same bitmaps stb returns.  Main thread only; cleared past
    // kMaxGlyphs.
    struct Glyph {
        std::vector<std::uint8_t> bitmap;
        int w = 0, h = 0, xoff = 0, yoff = 0;
    };
    const Glyph& glyph(int codepoint) const;
    static constexpr std::size_t kMaxGlyphs = 4096;
    mutable std::unordered_map<std::uint64_t, Glyph> glyphs_;
    // draw_banner's per-column flicker, reused across calls (main thread).
    mutable std::vector<float> col_term_;
    mutable std::vector<std::uint8_t> col_done_;

    std::vector<std::uint8_t> font_data_;
    int cap_px_ = 0;         // active cap height in px (last set_cap_px arg)
    float px_scale_ = 0;     // stb scale factor for the active cap height
    void* info_ = nullptr;   // stbtt_fontinfo*
    std::vector<std::uint8_t> info_storage_;
    // Probe metrics from load(): a 100 px probe gives a cap "S" of
    // probe_cap_px_, so px_scale_ = probe_scale_ * cap_px / probe_cap_px_.
    float probe_scale_ = 0;
    float probe_cap_px_ = 0;
};

}  // namespace olduvai::enhance
