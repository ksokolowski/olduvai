// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Menu overlay renderer — direction-C translucent slab + centered list with a
// left accent-bar cursor, drawn into the 320×200 framebuffer with the CHARSET1
// bitmap font (it upscales with everything else).  HD vector-font rendering is
// a later enhancement.

#pragma once

#include <cstdint>
#include <vector>

#include "enhance/hd_text.hpp"
#include "formats/mat.hpp"
#include "presentation/confirm_dialog.hpp"
#include "presentation/game_render.hpp"
#include "presentation/menu.hpp"

namespace olduvai::presentation {

// Geometry of the menu slab + rows in NATIVE 320×200 coordinates, shared by
// the bitmap renderer (draw_menu) and the HD vector-text pass so they line up.
struct MenuLayout {
    int slab_x = 0, slab_y = 0, slab_w = 0, slab_h = 0;
    int header_baseline = 0;   // native y baseline of the centered header
    int row0_baseline = 0;     // native y baseline of the first row
    int row_h = 0;
    int label_x = 0;           // native x of row labels
    int value_right = 0;       // native x of the right edge for values
    int accent_x = 0;          // native x of the cursor accent bar
};

MenuLayout compute_menu_layout(const Menu& menu, int fb_w, int fb_h);

// Draw the menu's current screen over `fb`. `dim` darkens the existing frame
// behind the slab (Pause overlay).  When `draw_text` is false the slab + accent
// bar are drawn but the bitmap glyphs are skipped — used in HD mode, where the
// text is rendered crisply by the vector-font overlay pass instead (matching
// the HUD's hybrid-font rule).
// `cursor_bone` (optional, with `bone_palette`): the game's score-bone sprite
// (LxSPR[33], art 25x13) as the selection pointer, drawn INSIDE the slab left
// of the label with its authentic in-game colours — except the red score
// digits (palette idx 5), which are remapped to the bone face (idx 15) so the
// pointer is a clean blank bone.  Native-res blit into `fb`, so the frame
// pipeline scales + filters it with the CURRENT --hd-profile like game art.
void draw_menu(FrameBuffer& fb, const Menu& menu,
               const std::vector<formats::Sprite>& charset, bool dim,
               bool draw_text = true,
               const formats::Sprite* cursor_bone = nullptr,
               const std::vector<formats::Rgb>* bone_palette = nullptr);

// Draw the menu's text with the HD vector font into an output-resolution RGBA
// overlay buffer (ow×oh), using the shared MenuLayout so it lines up with the
// slab/accent drawn by draw_menu(..., draw_text=false).  Used in enhanced mode
// by both the in-game Pause overlay and the title-screen Main menu.
// `title_tsec` = wall-clock seconds, drives the animated caveman fire-blood
// shader applied to the OLDUVAI title header (other headers stay flat).
// `fx/fy/fw/fh` (optional): the OUTPUT-pixel rect of the native 320x200 frame
// inside the overlay buffer.  In widescreen the frame is pillarboxed at
// ws_margin, so mapping through ow/320 alone stretched the glyphs across the
// full wide canvas while the slab stayed centred (the misaligned menus).
// Defaults (-1) mean "the frame fills the buffer" — the classic mapping.
// The font cap is scaled by fw/ow internally so glyph size follows the frame.
void draw_menu_vector(std::vector<std::uint8_t>& buf, int ow, int oh,
                      enhance::HdText& font, const Menu& menu,
                      float title_tsec = 0.0f, int fx = -1, int fy = -1,
                      int fw = -1, int fh = -1);

// Draw the confirm/discard dialog slab over `fb`.  `dim` darkens the existing
// frame (same Pause-overlay idiom as draw_menu).  When `draw_text` is false
// the slab + button highlight are drawn but glyphs are skipped — used in HD
// mode where the vector overlay supplies crisp text.
void draw_confirm(FrameBuffer& fb, const ConfirmDialog& dlg,
                  const std::vector<formats::Sprite>& charset, bool dim,
                  bool draw_text = true);

// HD vector-text pass for the confirm dialog.  Mirrors draw_menu_vector:
// draws into an output-resolution RGBA overlay buffer (ow×oh) using the
// shared slab geometry so it lines up with draw_confirm(..., draw_text=false).
void draw_confirm_vector(std::vector<std::uint8_t>& buf, int ow, int oh,
                         enhance::HdText& font, const ConfirmDialog& dlg,
                         int fx = -1, int fy = -1, int fw = -1,
                         int fh = -1);

}  // namespace olduvai::presentation
