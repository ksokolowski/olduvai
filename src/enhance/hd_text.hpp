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

namespace olduvai::enhance {

class BannerShader;

class HdText {
public:
    // Loads the bundled font (next to the executable, or from
    // OLDUVAI_FONT).  scale = HD target scale (2/3/4); sets the active cap
    // height to 8*scale px (the HD-compose metric).  `font_file` selects the
    // vector face by file name (default "FreckleFace-Regular.ttf"; the
    // reference --hd-font noto maps to "NotoSans-Regular.ttf").
    bool load(const std::string& exe_dir, int scale,
              const std::string& font_file = "FreckleFace-Regular.ttf");
    bool ok() const { return !font_data_.empty(); }

    // One-time stderr guidance when an HD feature wanted the vector face
    // and load() failed: says what was searched and where to get the font
    // (the release "fonts" folder, or Google Fonts — OFL-licensed).
    // ASCII-only output so it renders on any console codepage.
    static void report_missing(const std::string& exe_dir,
                               const std::string& font_file);

    // Re-size the active em so a capital "S" spans `cap_px` pixels.  Used by
    // the output-resolution text overlay, where the cap height is
    // 8 * output_w/320 (the 8 px native cap scaled to the physical window),
    // independent of the HD compose scale.  measure()/draw() use this size.
    void set_cap_px(int cap_px);

    // Active cap height in px (the last value passed to set_cap_px).  Lets a
    // caller that temporarily re-sizes the font for one draw restore the prior
    // size for subsequent draws in the same overlay pass (e.g. wide HUD text
    // then a pause menu).
    int cap_px() const { return cap_px_; }

    // Width of `text` in pixels at the active em.
    int measure(const std::string& text) const;

    // Draw with the baseline at HD (x, y); alpha-blended.
    void draw(std::vector<std::uint8_t>& rgba, int buf_w, int buf_h, int x,
              int baseline_y, const std::string& text, std::uint8_t r,
              std::uint8_t g, std::uint8_t b) const;

    // Per-pixel banner effect (enhance/banner_shader.hpp): each covered pixel
    // is coloured by the shader at (u, v), u spanning the text width and v the
    // cap height.  The shader is inlined into the glyph loop — it was a
    // std::function per pixel — and its column term (the flicker) is computed
    // once per column.  Coverage alpha-blending is identical to draw().
    void draw_banner(std::vector<std::uint8_t>& rgba, int buf_w, int buf_h,
                     int x, int baseline_y, const std::string& text,
                     const BannerShader& shader) const;

private:
    // The glyph walk both draw() and draw_banner() perform: for each character,
    // rasterise, blend its coverage into `rgba` clipped to the buffer, then
    // advance the pen by the advance width plus the kerning pair.  The two
    // differed ONLY in where the ink colour came from — fixed for draw(), a
    // banner shader over normalised (u, v) for draw_banner() — so `color`
    // supplies it per pixel and everything else is written once.
    //
    // A template rather than a std::function so draw()'s constant colour costs
    // nothing: this is the HUD text path, called per glyph pixel per frame.
    // Defined in hd_text.cpp — both instantiations live in that TU.
    template <typename ColorFn>
    void rasterise(std::vector<std::uint8_t>& rgba, int buf_w, int buf_h, int x,
                   int baseline_y, const std::string& text,
                   ColorFn color) const;

    // Rasterised glyphs, keyed by (active scale, codepoint).  stb_truetype
    // rebuilds a glyph from its curves (and mallocs the bitmap) on every
    // call, and the overlay redraws animated text — the GET READY / NOT
    // ENOUGH FOOD banners, the tally — on every present; a cache turns that
    // into a copy.  The bitmaps are the ones stb would return, so the output
    // is unchanged.  Main thread only (text is never drawn from the scaler
    // workers); cleared when it grows past kMaxGlyphs (a few sizes x ASCII
    // is a few hundred entries — only window resizes add more).
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
    // Cached probe metrics from load() so set_cap_px() can recompute
    // px_scale_ for any cap height without re-initialising the font:
    // a probe at 100 px gives a cap-"S" of probe_cap_px_ pixels, so
    // px_scale_ = probe_scale_ * cap_px / probe_cap_px_.
    float probe_scale_ = 0;
    float probe_cap_px_ = 0;
};

}  // namespace olduvai::enhance
