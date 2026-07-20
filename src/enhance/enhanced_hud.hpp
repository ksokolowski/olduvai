// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The locked enhanced HUD — grouped 3-column grid with vector labels and
// gradient status bars.  Native-coordinate design:
//   rows: TOP=2, R1 baseline 10, R2 baseline 21, bar base on R2 (y=13).
//   column A at x=0: Score over Food; column B (+18 gap): Lives over
//   Energy; column C: Time right-aligned to x=318.  Labels title-case,
//   no colons, gaps measured from real glyph widths (LABEL_GAP=6).
//   Bars: 41x8, 1px white border, dark empty fill; food fills
//   continuously (39 * food/46), energy tiles ten 3px pips edge-to-edge;
//   one status colour per meter by fill fraction (red->yellow->green).
//
// SPLIT RENDER (output-resolution text).  The NON-TEXT elements (gauge
// boxes, food fill, energy pips) are drawn into the HD compose buffer at the
// compose scale; the VECTOR TEXT (labels + numeric values) is drawn into a
// separate output-resolution overlay so the glyphs stay crisp at the physical
// window resolution (see presentation/text_overlay).  Both share one native-
// coordinate layout (computed from native glyph widths via HdText at native
// cap height) so the numbers sit over the same gauges in both passes.

#pragma once

#include <cstdint>

#include "enhance/hd_text.hpp"
#include "systems/player.hpp"

namespace olduvai::enhance {

// The native-coordinate layout shared by the bar pass and the text pass.
// All coordinates are NATIVE (320×200 design space); each pass scales them by
// its own target factor.  `baseline` y values are text baselines.
struct EnhancedHudLayout {
    struct TextItem {
        int x;            // native left x (baseline-left origin)
        int baseline_y;   // native text baseline
        std::string str;
        std::uint8_t r, g, b;
    };
    std::vector<TextItem> texts;

    struct Box { int x, y, w, h; };          // gauge outline (native)
    struct Fill { int x, y, w, h; int r, g, b; };  // food fill / energy pip

    std::vector<Box> boxes;
    std::vector<Fill> fills;
};

// Compute the native-coordinate layout for the current state.  `text` is used
// only to MEASURE glyph widths; it is left set to native cap height (8 px) on
// return.  No drawing happens here.
EnhancedHudLayout compute_enhanced_hud_layout(HdText& text,
                                              const systems::SystemsState& state);

// Draw the NON-TEXT HUD elements (gauge boxes, food fill, energy pips) into a
// `scale`-resolution RGBA buffer (buf_w == 320*scale, buf_h == 200*scale, OR a
// wider widescreen buffer).  Native layout coordinates are multiplied by
// `scale`.  `x_off_native` is added (in NATIVE px, before scaling) to every
// box/fill x — used by the widescreen path to place the bars at the centre
// offset (ws_margin) inside the wide buffer.  No vector text.
void draw_enhanced_hud_bars(std::vector<std::uint8_t>& rgba, int buf_w,
                            int buf_h, int scale,
                            const EnhancedHudLayout& layout,
                            int x_off_native = 0);

// Draw the vector TEXT of the layout into an output-resolution RGBA overlay
// (out_w × out_h).  Native x is multiplied by out_w/320, native baseline_y by
// out_h/200.  `text` must already be sized to the output cap height by the
// caller (TextOverlay::begin → HdText::set_cap_px).
void draw_enhanced_hud_text(std::vector<std::uint8_t>& rgba, int out_w,
                            int out_h, const HdText& text,
                            const EnhancedHudLayout& layout);

}  // namespace olduvai::enhance
