// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// HUD — score/lives/timer digits, food + energy bars, ENERGY label, and
// the GET READY banner.  Glyphs come from the 1bpp font container; y is
// the BASELINE (glyph top = y - height) and advance is proportional.
//
// Layout (all positions original):       // FUN_27f7_13d4/15a8/166d/1756/1501
//   score 6 digits @ (48, 8) · lives 2 @ (232, 8) · timer 2 @ (296, 8)
//   food bar @ (128, 0, 46x6) + borders/dividers · cap 46 writeback
//   energy bg @ (64, 10, 41x7) + pips (65+4i, 11, 2x5) · "ENERGY:" @ (4, 18)
//   timer turns palette-5 when < 11
// GET READY banner: sprites 132 @ (128,100) + 133 @ (162,97), visible while
// 2 <= counter <= 17, decrement on even frames.  // FUN_27f7_1277

#pragma once

#include "presentation/game_render.hpp"

namespace olduvai::presentation {

// Draw `text` with the 1bpp font at baseline y; palette index `color_idx`.
void draw_text(FrameBuffer& fb, const std::vector<formats::Sprite>& charset,
               const std::vector<formats::Rgb>& pal, int x, int y,
               const std::string& text, int color_idx = 15);

// Same, but with a direct RGB colour (no palette dependency) — used by the
// menu overlay, whose colours aren't part of any game palette.
void draw_text_rgb(FrameBuffer& fb, const std::vector<formats::Sprite>& charset,
                   int x, int y, const std::string& text, formats::Rgb color);

// Proportional pixel width of `text` in the 1bpp font (for centering /
// right-alignment). Mirrors draw_text's advance (glyph width, else 8).
int text_width(const std::vector<formats::Sprite>& charset, const std::string& text);

// Full HUD; mutates state (food cap writeback, GET READY decrement).
// hd_text=true suppresses the bitmap digits/bars/labels (the enhanced
// vector HUD draws them over the upscaled frame instead) while keeping
// the GET READY banner and the state writebacks.
void draw_hud(FrameBuffer& fb, systems::SystemsState& state,
              const std::vector<formats::Sprite>& charset,
              const std::vector<formats::Sprite>& spr_mat,
              const std::vector<formats::Rgb>& pal, bool hd_text = false);

}  // namespace olduvai::presentation
