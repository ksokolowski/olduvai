// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/text_overlay_edit.hpp"

#include <algorithm>

#include "presentation/hud_render.hpp"   // draw_text_rgb, text_width

namespace olduvai::presentation {

namespace {

using formats::Rgb;

// Same slab/text vocabulary as menu_render (kept local — those are file-static
// there).  Charcoal slab, light-grey text, white for the focused element.
constexpr Rgb kText{205, 214, 224};
constexpr Rgb kSelected{255, 255, 255};
constexpr Rgb kDim{120, 128, 140};
constexpr int kVisibleRows = 13;   // lines of the text area shown at once
constexpr int kCharW = 8;          // proportional font ~advance for layout

// Alpha-blend a filled rect into a native FrameBuffer.
void fb_blend_rect(FrameBuffer& fb, int x, int y, int w, int h, Rgb c, int a) {
    for (int yy = std::max(0, y); yy < std::min(fb.h, y + h); ++yy) {
        for (int xx = std::max(0, x); xx < std::min(fb.w, x + w); ++xx) {
            const std::size_t o = (static_cast<std::size_t>(yy) * fb.w + xx) * 4;
            fb.px[o] = static_cast<std::uint8_t>((c.r * a + fb.px[o] * (255 - a)) / 255);
            fb.px[o + 1] = static_cast<std::uint8_t>((c.g * a + fb.px[o + 1] * (255 - a)) / 255);
            fb.px[o + 2] = static_cast<std::uint8_t>((c.b * a + fb.px[o + 2] * (255 - a)) / 255);
            fb.px[o + 3] = 255;
        }
    }
}

// Alpha-blend a filled rect into an output-res RGBA buffer.
void buf_blend_rect(std::vector<std::uint8_t>& buf, int ow, int oh,
                    int x, int y, int w, int h, Rgb c, int a) {
    for (int yy = std::max(0, y); yy < std::min(oh, y + h); ++yy) {
        for (int xx = std::max(0, x); xx < std::min(ow, x + w); ++xx) {
            const std::size_t o = (static_cast<std::size_t>(yy) * ow + xx) * 4;
            buf[o] = static_cast<std::uint8_t>((c.r * a + buf[o] * (255 - a)) / 255);
            buf[o + 1] = static_cast<std::uint8_t>((c.g * a + buf[o + 1] * (255 - a)) / 255);
            buf[o + 2] = static_cast<std::uint8_t>((c.b * a + buf[o + 2] * (255 - a)) / 255);
            buf[o + 3] = 255;
        }
    }
}

// The visual row (index into the wrapped list) that holds the caret, and the
// caret's byte offset within that visual row's text.
struct CaretPos { int vis = 0; int x = 0; };
CaretPos caret_in(const std::vector<VisRow>& rows, int crow, int ccol) {
    CaretPos out;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (rows[static_cast<std::size_t>(i)].lrow != crow) continue;
        const int start = rows[static_cast<std::size_t>(i)].start;
        const int len = static_cast<int>(rows[static_cast<std::size_t>(i)].text.size());
        // Last wrapped row of a logical line owns the end-of-line caret.
        const bool last_of_line =
            i + 1 >= static_cast<int>(rows.size()) ||
            rows[static_cast<std::size_t>(i + 1)].lrow != crow;
        if (ccol >= start && (ccol < start + len ||
                              (last_of_line && ccol <= start + len))) {
            out.vis = i;
            out.x = ccol - start;
            return out;
        }
        out.vis = i;   // fallback: keep the last matching row
        out.x = len;
    }
    return out;
}

// First visible VISUAL row so the caret's visual row stays on screen.
int scroll_top_vis(int caret_vis) {
    return caret_vis < kVisibleRows ? 0 : caret_vis - (kVisibleRows - 1);
}

}  // namespace

std::vector<VisRow> wrap_lines(const std::vector<std::string>& lines, int cols) {
    if (cols < 1) cols = 1;
    std::vector<VisRow> out;
    for (int lr = 0; lr < static_cast<int>(lines.size()); ++lr) {
        const std::string& L = lines[static_cast<std::size_t>(lr)];
        if (L.empty()) { out.push_back({"", lr, 0}); continue; }
        std::size_t pos = 0;
        while (pos < L.size()) {
            const std::size_t remaining = L.size() - pos;
            if (remaining <= static_cast<std::size_t>(cols)) {
                out.push_back({L.substr(pos), lr, static_cast<int>(pos)});
                break;
            }
            // Prefer a break at the last space within the window.
            std::size_t brk = L.rfind(' ', pos + static_cast<std::size_t>(cols));
            std::size_t next;
            std::size_t take;
            if (brk != std::string::npos && brk > pos) {
                take = brk - pos;             // chunk excludes the space
                next = brk + 1;               // skip the space
            } else {
                take = static_cast<std::size_t>(cols);   // hard break
                next = pos + take;
            }
            out.push_back({L.substr(pos, take), lr, static_cast<int>(pos)});
            pos = next;
        }
    }
    if (out.empty()) out.push_back({"", 0, 0});
    return out;
}

EditResult edit_handle_event(EditOverlayState& st, const SDL_Event& ev) {
    if (ev.type == SDL_TEXTINPUT) {
        if (st.focus == EditFocus::kText) st.editor.insert(ev.text.text);
        return EditResult::kNone;
    }
    if (ev.type != SDL_KEYDOWN) return EditResult::kNone;
    const SDL_Keycode k = ev.key.keysym.sym;
    const bool ctrl = (ev.key.keysym.mod & KMOD_CTRL) != 0;
    if (k == SDLK_ESCAPE) return EditResult::kCancel;
    if (ctrl && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) return EditResult::kSave;
    if (k == SDLK_TAB) {
        const bool back = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
        switch (st.focus) {
            case EditFocus::kText:
                st.focus = back ? EditFocus::kCancel : EditFocus::kSave; break;
            case EditFocus::kSave:
                st.focus = back ? EditFocus::kText : EditFocus::kCancel; break;
            case EditFocus::kCancel:
                st.focus = back ? EditFocus::kSave : EditFocus::kText; break;
        }
        return EditResult::kNone;
    }
    if (st.focus == EditFocus::kSave &&
        (k == SDLK_RETURN || k == SDLK_KP_ENTER)) return EditResult::kSave;
    if (st.focus == EditFocus::kCancel &&
        (k == SDLK_RETURN || k == SDLK_KP_ENTER)) return EditResult::kCancel;
    if (st.focus != EditFocus::kText) return EditResult::kNone;
    switch (k) {
        case SDLK_RETURN: case SDLK_KP_ENTER: st.editor.newline(); break;
        case SDLK_BACKSPACE: st.editor.backspace(); break;
        case SDLK_DELETE:    st.editor.del(); break;
        case SDLK_LEFT:  st.editor.move(TextEditor::Move::kLeft); break;
        case SDLK_RIGHT: st.editor.move(TextEditor::Move::kRight); break;
        case SDLK_UP:    st.editor.move(TextEditor::Move::kUp); break;
        case SDLK_DOWN:  st.editor.move(TextEditor::Move::kDown); break;
        case SDLK_HOME:  st.editor.move(TextEditor::Move::kHome); break;
        case SDLK_END:   st.editor.move(TextEditor::Move::kEnd); break;
        default: break;
    }
    return EditResult::kNone;
}

void draw_edit_overlay(FrameBuffer& fb,
                       const std::vector<formats::Sprite>& charset,
                       const EditOverlayState& st) {
    // Near-full-canvas slab.
    const int sx = 10, sy = 8, sw = fb.w - 20, sh = fb.h - 16;
    fb_blend_rect(fb, sx, sy, sw, sh, Rgb{16, 20, 27}, 232);
    const int lx = sx + 8;
    int y = sy + 8;
    draw_text_rgb(fb, charset, lx, y, st.title, kSelected);
    y += 14;
    const int cols = (sw - 16) / kCharW;              // fit the slab width
    const std::vector<VisRow> rows = wrap_lines(st.editor.lines(), cols);
    const CaretPos cp = caret_in(rows, st.editor.row(), st.editor.col());
    const int top = scroll_top_vis(cp.vis);
    for (int i = 0; i < kVisibleRows; ++i) {
        const int vi = top + i;
        if (vi >= static_cast<int>(rows.size())) break;
        const int ry = y + i * 10;
        draw_text_rgb(fb, charset, lx, ry,
                      rows[static_cast<std::size_t>(vi)].text, kText);
        if (st.focus == EditFocus::kText && vi == cp.vis) {
            fb_blend_rect(fb, lx + cp.x * kCharW, ry - 8, 2, 10, kSelected, 220);
        }
    }
    // Buttons + footer.
    const int by = sy + sh - 22;
    const Rgb save_c = st.focus == EditFocus::kSave ? kSelected : kText;
    const Rgb cancel_c = st.focus == EditFocus::kCancel ? kSelected : kText;
    // Focused button = white (kSelected); the font lacks [] so no brackets.
    draw_text_rgb(fb, charset, lx, by,
                  st.focus == EditFocus::kSave ? "> SAVE" : "  Save", save_c);
    draw_text_rgb(fb, charset, lx + 72, by,
                  st.focus == EditFocus::kCancel ? "> CANCEL" : "  Cancel", cancel_c);
    draw_text_rgb(fb, charset, lx, by + 11, "Tab move  Esc cancel", kDim);
}

void draw_edit_overlay_vector(std::vector<std::uint8_t>& buf, int ow, int oh,
                              enhance::HdText& font, const EditOverlayState& st) {
    const int sx = ow / 24, sy = oh / 20, sw = ow - 2 * sx, sh = oh - 2 * sy;
    buf_blend_rect(buf, ow, oh, sx, sy, sw, sh, Rgb{16, 20, 27}, 232);
    const int cap = std::max(10, oh / 22);
    font.set_cap_px(cap);
    const int lh = cap + 4;
    const int lx = sx + cap;
    int y = sy + cap + 4;
    font.draw(buf, ow, oh, lx, y, st.title, 255, 255, 255);
    y += lh + 4;
    // Proportional font: estimate columns from the average glyph advance
    // (~0.55*cap) so wrapping roughly fills the width.
    const int cols = std::max(16, (sw - 2 * cap) * 20 / (cap * 11));
    const std::vector<VisRow> rows = wrap_lines(st.editor.lines(), cols);
    const CaretPos cp = caret_in(rows, st.editor.row(), st.editor.col());
    const int top = scroll_top_vis(cp.vis);
    for (int i = 0; i < kVisibleRows; ++i) {
        const int vi = top + i;
        if (vi >= static_cast<int>(rows.size())) break;
        const int ry = y + i * lh;
        const VisRow& vr = rows[static_cast<std::size_t>(vi)];
        font.draw(buf, ow, oh, lx, ry, vr.text, kText.r, kText.g, kText.b);
        if (st.focus == EditFocus::kText && vi == cp.vis) {
            const int cx = lx + font.measure(
                vr.text.substr(0, static_cast<std::size_t>(cp.x)));
            buf_blend_rect(buf, ow, oh, cx, ry - cap, 2, lh, kSelected, 220);
        }
    }
    const int by = sy + sh - 2 * lh;
    const Rgb save_c = st.focus == EditFocus::kSave ? kSelected : kText;
    const Rgb cancel_c = st.focus == EditFocus::kCancel ? kSelected : kText;
    font.draw(buf, ow, oh, lx, by,
              st.focus == EditFocus::kSave ? "> SAVE" : "  Save",
              save_c.r, save_c.g, save_c.b);
    font.draw(buf, ow, oh, lx + font.measure("> SAVE     "), by,
              st.focus == EditFocus::kCancel ? "> CANCEL" : "  Cancel",
              cancel_c.r, cancel_c.g, cancel_c.b);
    font.draw(buf, ow, oh, lx, by + lh, "Tab move   Esc cancel",
              kDim.r, kDim.g, kDim.b);
}

}  // namespace olduvai::presentation
