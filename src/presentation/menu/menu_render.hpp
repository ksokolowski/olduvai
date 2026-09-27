// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Menu overlay renderer: a translucent slab and a centred list with a left
// accent-bar cursor, in the 320x200 framebuffer with the CHARSET1 bitmap font;
// the HD vector text is a separate pass (draw_menu_vector).

#pragma once

#include <cstdint>
#include <vector>

#include "enhance/hd_text.hpp"
#include "formats/mat.hpp"
#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/menu/menu.hpp"

namespace olduvai::presentation {

// Menu slab + row geometry in native 320x200 coordinates, shared by draw_menu
// and the vector pass so they line up.
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

// Draw the menu's current screen over `fb`.  `dim` darkens the frame behind
// the slab (Pause).  `draw_text` false: slab and accent bar only (HD, where the
// vector pass draws the text).  `cursor_bone` (with `bone_palette`): the score
// bone (LxSPR[33]) as the pointer, in its game colours except the red digits
// (idx 5 -> bone face, idx 15); blitted native, so it scales with the frame.
void draw_menu(FrameBuffer& fb, const Menu& menu,
               const std::vector<formats::Sprite>& charset, bool dim,
               bool draw_text = true,
               const formats::Sprite* cursor_bone = nullptr,
               const std::vector<formats::Rgb>* bone_palette = nullptr);

// The output sub-rect a menu lays itself out in: the pillarboxed centre under
// widescreen, else the whole buffer (the default).
struct MenuFrame {
    int x = -1;
    int y = -1;
    int w = -1;
    int h = -1;

    // Resolve the sentinels against the output size.  Call once, at the top.
    MenuFrame resolved(int ow, int oh) const {
        MenuFrame r = *this;
        if (r.w <= 0) { r.x = 0; r.w = ow; }
        if (r.h <= 0) { r.y = 0; r.h = oh; }
        return r;
    }
    // Where a logical_w x logical_h picture lands in an ow x oh output: the
    // rect SDL_RenderSetLogicalSize letterboxes it into, or the whole output
    // with no logical size (0 x 0, "stretch").  sub_x / sub_w (logical units)
    // narrow it to a slice (the centre 320 of a wide frame).  The overlay is
    // drawn at output resolution, so it must be told where the picture is.
    static MenuFrame picture(int ow, int oh, int logical_w, int logical_h,
                             int sub_x = 0, int sub_w = -1) {
        if (logical_w <= 0 || logical_h <= 0) return {0, 0, ow, oh};
        int vw = ow, vh = oh;
        if (static_cast<long long>(ow) * logical_h <=
            static_cast<long long>(oh) * logical_w)
            vh = logical_h * ow / logical_w;   // width-limited: bars top/bottom
        else
            vw = logical_w * oh / logical_h;   // height-limited: bars left/right
        MenuFrame f{(ow - vw) / 2, (oh - vh) / 2, vw, vh};
        if (sub_w > 0) {
            f.x += sub_x * vw / logical_w;
            f.w = sub_w * vw / logical_w;
        }
        return f;
    }
    // Map a native 320x200 coordinate into the frame.
    int sx(int nx) const { return x + nx * w / 320; }
    int sy(int ny) const { return y + ny * h / 200; }
};

// The menu's text in the HD vector font into an output-resolution overlay
// (ow x oh), on the same MenuLayout as draw_menu(..., draw_text=false).  Used
// by the Pause overlay and the title menu.  `title_tsec` (seconds) animates the
// OLDUVAI title header's fire shader.  `frame`: where the native frame sits in
// the buffer; the font cap scales with it.
void draw_menu_vector(const enhance::Canvas& cv,
                      enhance::HdText& font, const Menu& menu,
                      float title_tsec = 0.0f, MenuFrame frame = {});

// Draw the confirm/discard dialog over `fb` (`dim` as in draw_menu).
// `draw_text` false: slab and button highlight only (HD).
void draw_confirm(FrameBuffer& fb, const ConfirmDialog& dlg,
                  const std::vector<formats::Sprite>& charset, bool dim,
                  bool draw_text = true);

// Vector-text pass for the confirm dialog, on the same slab geometry as
// draw_confirm(..., draw_text=false).
void draw_confirm_vector(const enhance::Canvas& cv,
                         enhance::HdText& font, const ConfirmDialog& dlg,
                         MenuFrame frame = {});

}  // namespace olduvai::presentation
