// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// HD vector text — TrueType glyph rendering (stb_truetype) for the
// enhanced HUD.  The font auto-sizes so the capital height lands at
// 8 px native × target scale, matching the locked design's metric.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace olduvai::enhance {

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

    // Per-pixel styled draw (enhanced banner effects).  `shade(u, v, r, g, b)`
    // sets the colour for each covered pixel, where u∈[0,1] spans the whole
    // text width (left→right) and v∈[0,1] spans the cap height (top→bottom).
    // This gives gradients (vertical = v, e.g. fire/gold), rainbows
    // (horizontal = u) and pulses/scrolls (fold a time phase into the closure).
    // Coverage alpha-blending is identical to draw().
    using ShadeFn = std::function<void(float u, float v, std::uint8_t& r,
                                       std::uint8_t& g, std::uint8_t& b)>;
    void draw_styled(std::vector<std::uint8_t>& rgba, int buf_w, int buf_h,
                     int x, int baseline_y, const std::string& text,
                     const ShadeFn& shade) const;

private:
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
