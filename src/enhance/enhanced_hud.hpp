// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The enhanced HUD: a 3-column grid with vector labels and gradient bars, in
// native coordinates:
//   rows: TOP=2, R1 baseline 10, R2 baseline 21, bars on R2 (y=13).
//   column A at x=0: Score over Food; B (+18 gap): Lives over Energy;
//   C: Time right-aligned to x=318.  Title-case labels, no colons, gaps from
//   real glyph widths (LABEL_GAP=6).
//   Bars 41x8, 1 px white border, dark fill; food fills continuously
//   (39 * food/46), energy is ten 3 px pips; one status colour per meter by
//   fill (red -> yellow -> green).
// Bars go into the HD compose buffer; the text into the output-resolution
// overlay (text_overlay).  Both use one native layout.

#pragma once

#include <cstdint>

#include "enhance/hd_text.hpp"
#include "systems/player.hpp"

namespace olduvai::enhance {

// Native (320x200) layout shared by the bar and text passes; each scales it.
struct EnhancedHudLayout {
    struct TextItem {
        int x;            // native left x (baseline-left origin)
        int baseline_y;   // native text baseline
        std::string str;
        formats::Rgb ink;
    };
    std::vector<TextItem> texts;

    struct Box { int x, y, w, h; };          // gauge outline (native)
    struct Fill { int x, y, w, h; int r, g, b; };  // food fill / energy pip

    std::vector<Box> boxes;
    std::vector<Fill> fills;
};

// Layout for the current state.  `text` only measures glyphs and is left at
// the native cap height (8 px).
EnhancedHudLayout compute_enhanced_hud_layout(HdText& text,
                                              const systems::SystemsState& state);

// Draw the bars (boxes, food fill, energy pips) into a `scale`-resolution
// buffer (320*scale wide, or wider in widescreen); native coordinates x scale.
// `x_off_native` shifts every x (the widescreen centre offset).  No text.
void draw_enhanced_hud_bars(const Canvas& cv, int scale,
                            const EnhancedHudLayout& layout,
                            int x_off_native = 0);

// Draw the layout's text into an output-resolution overlay, mapped into the
// picture's rect: x -> pic.x + x*pic.w/320, baseline y -> pic.y + y*pic.h/200
// (an empty rect: the whole canvas).  `text` must already be sized (8 native
// px -> 8*pic.w/320).
void draw_enhanced_hud_text(const Canvas& cv, const HdText& text,
                            const EnhancedHudLayout& layout, Rect pic = {});

}  // namespace olduvai::enhance
