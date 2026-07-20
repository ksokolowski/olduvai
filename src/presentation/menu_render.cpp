// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/menu_render.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "presentation/banner_fx.hpp"
#include "presentation/game_render.hpp"
#include "presentation/hud_render.hpp"

// ── ConfirmDialog layout constants ────────────────────────────────────────────
// Native 320×200 geometry, sized to fit: title + change rows + optional note
// + two side-by-side buttons.  Shares the same slab/blend/palette vocabulary
// as draw_menu so the two overlays look coherent.
namespace {
static constexpr int kDlgSlabW   = 240;
static constexpr int kDlgPad     = 9;
static constexpr int kDlgLineH   = 11;
static constexpr int kDlgHeaderH = 16;
static constexpr int kDlgNoteH   = 10;   // note line height
static constexpr int kDlgBtnH    = 14;   // button row height
static constexpr int kDlgBtnGap  = 8;    // gap between the two buttons
}  // namespace

namespace olduvai::presentation {

namespace {

using formats::Rgb;

// Direction-C palette (see spec §5).
constexpr Rgb kHeader{127, 209, 255};   // cyan
constexpr Rgb kText{205, 214, 224};     // light grey
constexpr Rgb kSelected{255, 255, 255}; // white
constexpr Rgb kValue{210, 220, 232};
constexpr Rgb kHint{125, 135, 148};     // dim
constexpr Rgb kAccent{158, 255, 160};   // green cursor bar

void blend_px(FrameBuffer& fb, int x, int y, Rgb c, int a) {
    if (x < 0 || y < 0 || x >= fb.w || y >= fb.h) return;
    const std::size_t i = (static_cast<std::size_t>(y) * fb.w + x) * 4;
    auto mix = [&](std::uint8_t dst, std::uint8_t src) {
        return static_cast<std::uint8_t>((src * a + dst * (255 - a)) / 255);
    };
    fb.px[i + 0] = mix(fb.px[i + 0], c.r);
    fb.px[i + 1] = mix(fb.px[i + 1], c.g);
    fb.px[i + 2] = mix(fb.px[i + 2], c.b);
    fb.px[i + 3] = 255;
}

void blend_rect(FrameBuffer& fb, int x, int y, int w, int h, Rgb c, int a) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) blend_px(fb, xx, yy, c, a);
}

}  // namespace

MenuLayout compute_menu_layout(const Menu& menu, int fb_w, int fb_h) {
    const int n = static_cast<int>(menu.rows().size());
    const int line_h = 11, header_h = 16, pad = 9, slab_w = 212;
    const int slab_h = pad * 2 + header_h + n * line_h;
    MenuLayout L;
    L.slab_w = slab_w;
    L.slab_h = slab_h;
    L.slab_x = (fb_w - slab_w) / 2;
    L.slab_y = (fb_h - slab_h) / 2;
    L.header_baseline = L.slab_y + pad + 8;
    L.row0_baseline = L.slab_y + pad + header_h + 8;
    L.row_h = line_h;
    // Room for the half-scale bone pointer (13px) + a tight 3px gap.
    L.label_x = L.slab_x + 24;
    L.value_right = L.slab_x + slab_w - 10;
    L.accent_x = L.slab_x + 7;
    return L;
}

void draw_menu(FrameBuffer& fb, const Menu& menu,
               const std::vector<formats::Sprite>& charset, bool dim,
               bool draw_text, const formats::Sprite* cursor_bone,
               const std::vector<formats::Rgb>* bone_palette) {
    const auto rows = menu.rows();
    const int n = static_cast<int>(rows.size());
    const MenuLayout L = compute_menu_layout(menu, fb.w, fb.h);

    if (dim) blend_rect(fb, 0, 0, fb.w, fb.h, Rgb{8, 11, 16}, 150);
    blend_rect(fb, L.slab_x, L.slab_y, L.slab_w, L.slab_h, Rgb{16, 20, 27}, 224);

    const int cursor = menu.cursor_index();
    for (int i = 0; i < n; ++i) {
        const int y = L.row0_baseline + i * L.row_h;
        if (i != cursor) continue;
        if (!draw_text) {
            // HD/enhanced: the vector overlay draws an anti-aliased bone at
            // glyph resolution (draw_menu_vector) — nothing native here.
        } else if (cursor_bone != nullptr && bone_palette != nullptr &&
                   bone_palette->size() >= 16) {
            // Selection pointer = the game's score bone (LxSPR[33]) in its
            // AUTHENTIC in-game colours at HALF SCALE (13x7 — about two font
            // chars wide), sitting tight against the label inside the slab.
            // The red score digits (idx 5) are remapped to the bone face
            // (idx 15) so it reads as a blank pointing bone.  Drawn into the
            // native frame → the pipeline scales + filters it with the
            // current --hd-profile like any game sprite.
            std::vector<formats::Rgb> pal = *bone_palette;
            pal[5] = pal[15];
            const auto pixels = cursor_bone->decode_indexed();
            const int sw = cursor_bone->width, sh = cursor_bone->height;
            const int bx = L.label_x - 3 - 13;   // right edge 3px before text
            const int by = y - 8;                // centred on the glyph band
            // (owner UX 2026-07-05: was y-6 — the bone sat visibly below
            // the glyph centre line in dos mode; -8 centres the 7px
            // half-scale bone on the 8px CHARSET1 band.)
            for (int sy = 0; sy + 1 < sh; sy += 2)
                for (int sx = 0; sx + 1 < sw; sx += 2) {
                    const auto& px =
                        pixels[static_cast<std::size_t>(sy) * sw + sx];
                    if (!px.opaque) continue;
                    const int dx = bx + sx / 2, dy = by + sy / 2;
                    if (dx < 0 || dy < 0 || dx >= fb.w || dy >= fb.h) continue;
                    const formats::Rgb c = (px.color < pal.size())
                                               ? pal[px.color]
                                               : formats::Rgb{255, 0, 255};
                    const std::size_t off =
                        (static_cast<std::size_t>(dy) * fb.w + dx) * 4;
                    fb.px[off] = c.r;
                    fb.px[off + 1] = c.g;
                    fb.px[off + 2] = c.b;
                    fb.px[off + 3] = 255;
                }
        } else {
            blend_rect(fb, L.accent_x, y - 7, 3, 9, kAccent, 255);  // accent bar
        }
    }
    if (!draw_text) return;  // HD mode: glyphs come from the vector overlay

    const std::string& hdr = menu.header();
    draw_text_rgb(fb, charset, L.slab_x + (L.slab_w - text_width(charset, hdr)) / 2,
                  L.header_baseline, hdr, kHeader);
    for (int i = 0; i < n; ++i) {
        const MenuRow& r = rows[i];
        const bool sel = (i == cursor);
        const int y = L.row0_baseline + i * L.row_h;
        draw_text_rgb(fb, charset, L.label_x, y, r.label, sel ? kSelected : kText);
        if (r.value) {
            const Rgb vc = (r.label == "Back" || !r.selectable) ? kHint : kValue;
            draw_text_rgb(fb, charset, L.value_right - text_width(charset, *r.value),
                          y, *r.value, vc);
        }
    }
}

void draw_menu_vector(std::vector<std::uint8_t>& buf, int ow, int oh,
                      enhance::HdText& font, const Menu& menu,
                      float title_tsec, int fx, int fy, int fw, int fh) {
    const MenuLayout L = compute_menu_layout(menu, 320, 200);
    const auto rows = menu.rows();
    // Frame rect: where the native 320x200 lives inside the overlay.  Full
    // buffer by default; in widescreen the caller passes the pillarboxed
    // centre so glyphs land ON the slab instead of stretching wide.
    if (fw <= 0) { fx = 0; fw = ow; }
    if (fh <= 0) { fy = 0; fh = oh; }
    auto sx = [&](int nx) { return fx + nx * fw / 320; };
    auto sy = [&](int ny) { return fy + ny * fh / 200; };
    // Glyph size follows the FRAME, not the whole canvas.
    const int entry_cap = font.cap_px();
    font.set_cap_px(std::max(1, entry_cap * fw / ow));
    const std::string hdr = menu.header();
    if (hdr == "OLDUVAI") {
        // The title is the showpiece: bigger (1.7× the menu cap), with a dark
        // charcoal-blood outline so the caveman fire-blood fill reads BOLD and
        // pops against the busy intro-cutscene background.  Cap is bumped just
        // for the title, then restored so the rows are unaffected.  Other
        // headers (OPTIONS / PAUSED / …) keep the flat accent blue below.
        const int menu_cap = font.cap_px();
        font.set_cap_px(std::max(1, menu_cap * 17 / 10));
        const int tx = fx + (fw - font.measure(hdr)) / 2;
        const int ty = sy(L.header_baseline);
        const int o = std::max(2, font.cap_px() / 9);   // outline thickness (px)
        for (int dy = -o; dy <= o; dy += o) {           // 8-way dark outline
            for (int dx = -o; dx <= o; dx += o) {
                if (dx || dy)
                    font.draw(buf, ow, oh, tx + dx, ty + dy, hdr, 18, 6, 6);
            }
        }
        const auto shade = make_banner_shade("caveman", title_tsec);
        font.draw_styled(buf, ow, oh, tx, ty, hdr, shade);
        font.draw_styled(buf, ow, oh, tx + 1, ty, hdr, shade);   // faux-bold
        font.set_cap_px(menu_cap);                       // restore for the rows
    } else {
        const int hdr_x = fx + (fw - font.measure(hdr)) / 2;
        font.draw(buf, ow, oh, hdr_x, sy(L.header_baseline), hdr, 127, 209, 255);
    }
    // Anti-aliased VECTOR bone pointer at glyph resolution — the game's
    // score-bone silhouette (shaft + two knobs per end) rebuilt as smooth
    // geometry so it matches the vector font's crispness instead of a
    // nearest-upscaled 13x7 sprite.  Sized from the active cap: ~2 glyphs
    // wide, centred on the selected row, tight against the label.
    auto draw_vector_bone = [&](int label_px_x, int baseline_y) {
        const float capf = static_cast<float>(font.cap_px());
        const float h = capf * 0.72f;          // bone height
        const float w = h * 1.9f;              // score-bone aspect (25:13)
        const float cy = static_cast<float>(baseline_y) - capf * 0.42f;
        const float x1 = static_cast<float>(label_px_x) - capf * 0.45f;
        const float x0 = x1 - w;
        const float r = h * 0.30f;             // end-knob radius
        const float rb = h * 0.20f;            // shaft half-thickness
        const float ky = h * 0.20f;            // knob vertical offset
        const float exl = x0 + r, exr = x1 - r;   // knob centres (x)
        auto sdf = [&](float px, float py) {
            // Signed distance to the bone silhouette (union of the shaft
            // capsule and the four end knobs); negative = inside.
            const float cxc = std::min(std::max(px, exl), exr);
            float d = std::hypot(px - cxc, py - cy) - rb;   // shaft
            for (const float kx : {exl, exr})
                for (const float sygn : {-1.0f, 1.0f})
                    d = std::min(d, std::hypot(px - kx,
                                               py - (cy + sygn * ky)) - r);
            return d;
        };
        const int lo_x = static_cast<int>(x0 - r - 2);
        const int hi_x = static_cast<int>(x1 + r + 2);
        const int lo_y = static_cast<int>(cy - h - 2);
        const int hi_y = static_cast<int>(cy + h + 2);
        const float aa = 0.8f;                 // AA band (px)
        const float ow_px = std::max(1.2f, capf / 12.0f);   // outline width
        for (int py2 = lo_y; py2 <= hi_y; ++py2) {
            if (py2 < 0 || py2 >= oh) continue;
            for (int px2 = lo_x; px2 <= hi_x; ++px2) {
                if (px2 < 0 || px2 >= ow) continue;
                const float d = sdf(static_cast<float>(px2) + 0.5f,
                                    static_cast<float>(py2) + 0.5f);
                if (d > ow_px + aa) continue;
                // Fill: bone-white with a soft under-shade; outline: dark.
                float fill_a = std::clamp((0.0f - d) / aa + 1.0f, 0.0f, 1.0f);
                float line_a =
                    std::clamp((ow_px - d) / aa + 0.0f, 0.0f, 1.0f);
                std::uint8_t cr, cg, cb;
                float a;
                if (fill_a > 0.0f) {
                    // vertical shade: lighter on top like the sprite art
                    const float v = std::clamp(
                        (static_cast<float>(py2) - (cy - h * 0.5f)) / h, 0.0f,
                        1.0f);
                    cr = static_cast<std::uint8_t>(232 - 60 * v);
                    cg = static_cast<std::uint8_t>(232 - 60 * v);
                    cb = static_cast<std::uint8_t>(236 - 58 * v);
                    a = fill_a;
                } else {
                    cr = 16; cg = 18; cb = 24;
                    a = line_a * 0.9f;
                }
                if (a <= 0.0f) continue;
                const std::size_t off =
                    (static_cast<std::size_t>(py2) * ow + px2) * 4;
                const float ia = 1.0f - a;
                buf[off] = static_cast<std::uint8_t>(cr * a + buf[off] * ia);
                buf[off + 1] =
                    static_cast<std::uint8_t>(cg * a + buf[off + 1] * ia);
                buf[off + 2] =
                    static_cast<std::uint8_t>(cb * a + buf[off + 2] * ia);
                buf[off + 3] = 255;
            }
        }
    };
    const int cursor = menu.cursor_index();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const MenuRow& r = rows[i];
        const bool sel = (i == cursor);
        const int by = sy(L.row0_baseline + i * L.row_h);
        if (sel) draw_vector_bone(sx(L.label_x), by);
        font.draw(buf, ow, oh, sx(L.label_x), by, r.label,
                  sel ? 255 : 205, sel ? 255 : 214, sel ? 255 : 224);
        if (r.value) {
            const bool dimv = (r.label == "Back" || !r.selectable);
            font.draw(buf, ow, oh, sx(L.value_right) - font.measure(*r.value),
                      by, *r.value, dimv ? 125 : 210, dimv ? 135 : 220,
                      dimv ? 148 : 232);
        }
    }
    font.set_cap_px(entry_cap);
}

// ── ConfirmDialog renderers ───────────────────────────────────────────────────

void draw_confirm(FrameBuffer& fb, const ConfirmDialog& dlg,
                  const std::vector<formats::Sprite>& charset, bool dim,
                  bool draw_text) {
    const int n_changes = static_cast<int>(dlg.changes().size());
    const bool has_note = !dlg.note().empty();

    // Slab height: pad + header + change rows + optional note + button row + pad
    const int slab_h = kDlgPad * 2 + kDlgHeaderH
                     + n_changes * kDlgLineH
                     + (has_note ? kDlgNoteH : 0)
                     + kDlgBtnH;
    const int slab_x = (fb.w - kDlgSlabW) / 2;
    const int slab_y = (fb.h - slab_h) / 2;

    if (dim) blend_rect(fb, 0, 0, fb.w, fb.h, Rgb{8, 11, 16}, 150);
    blend_rect(fb, slab_x, slab_y, kDlgSlabW, slab_h, Rgb{16, 20, 27}, 224);

    // Button highlight: accent-colored background on the selected button cell.
    // Two buttons side by side, centred in the slab.
    //   [ Apply ]   [ Discard ]
    // Each cell is 80px wide; total 160px; centred in 240px → 40px margin each.
    const int btn_y    = slab_y + slab_h - kDlgPad - kDlgBtnH + 2;
    const int btn_w    = 80;
    const int btn_area = btn_w * 2 + kDlgBtnGap;
    const int btn0_x   = slab_x + (kDlgSlabW - btn_area) / 2;
    const int btn1_x   = btn0_x + btn_w + kDlgBtnGap;

    // Highlight selected button.
    if (dlg.apply_selected())
        blend_rect(fb, btn0_x, btn_y - 1, btn_w, kDlgBtnH - 2, kAccent, 60);
    else
        blend_rect(fb, btn1_x, btn_y - 1, btn_w, kDlgBtnH - 2, kAccent, 60);

    if (!draw_text) return;

    // Title (header style, centred).
    const std::string& hdr = dlg.title();
    draw_text_rgb(fb, charset,
                  slab_x + (kDlgSlabW - text_width(charset, hdr)) / 2,
                  slab_y + kDlgPad + 8,
                  hdr, kHeader);

    // Change rows: "<label>:  <old> -> <new>"
    const int row0_y = slab_y + kDlgPad + kDlgHeaderH + 8;
    const int label_x = slab_x + 12;
    for (int i = 0; i < n_changes; ++i) {
        const StagedChange& c = dlg.changes()[i];
        const int ry = row0_y + i * kDlgLineH;
        std::string row = c.label + ":  " + c.old_value + " -> " + c.new_value;
        draw_text_rgb(fb, charset, label_x, ry, row, kText);
    }

    // Note (dimmed, centred).
    if (has_note) {
        const std::string& note = dlg.note();
        const int note_y = row0_y + n_changes * kDlgLineH + 4;
        draw_text_rgb(fb, charset,
                      slab_x + (kDlgSlabW - text_width(charset, note)) / 2,
                      note_y, note, kHint);
    }

    // Buttons.
    const Rgb apply_col   = dlg.apply_selected()  ? kSelected : kText;
    const Rgb discard_col = !dlg.apply_selected() ? kSelected : kText;
    const std::string apply_label   = "[ Apply ]";
    const std::string discard_label = "[ Discard ]";
    draw_text_rgb(fb, charset,
                  btn0_x + (btn_w - text_width(charset, apply_label)) / 2,
                  btn_y + 8, apply_label, apply_col);
    draw_text_rgb(fb, charset,
                  btn1_x + (btn_w - text_width(charset, discard_label)) / 2,
                  btn_y + 8, discard_label, discard_col);
}

void draw_confirm_vector(std::vector<std::uint8_t>& buf, int ow, int oh,
                         enhance::HdText& font, const ConfirmDialog& dlg,
                         int fx, int fy, int fw, int fh) {
    const int n_changes = static_cast<int>(dlg.changes().size());
    const bool has_note = !dlg.note().empty();

    // Reproduce the native layout in native coords, then scale to output.
    const int slab_h = kDlgPad * 2 + kDlgHeaderH
                     + n_changes * kDlgLineH
                     + (has_note ? kDlgNoteH : 0)
                     + kDlgBtnH;
    const int slab_x = (320 - kDlgSlabW) / 2;
    const int slab_y = (200 - slab_h) / 2;

    if (fw <= 0) { fx = 0; fw = ow; }
    if (fh <= 0) { fy = 0; fh = oh; }
    auto sx = [&](int nx) { return fx + nx * fw / 320; };
    auto sy = [&](int ny) { return fy + ny * fh / 200; };
    const int entry_cap = font.cap_px();
    font.set_cap_px(std::max(1, entry_cap * fw / ow));

    // Title.
    const std::string& hdr = dlg.title();
    font.draw(buf, ow, oh,
              sx(slab_x) + (kDlgSlabW * fw / 320 - font.measure(hdr)) / 2,
              sy(slab_y + kDlgPad + 8),
              hdr, 127, 209, 255);

    // Change rows.
    const int row0_y = slab_y + kDlgPad + kDlgHeaderH + 8;
    for (int i = 0; i < n_changes; ++i) {
        const StagedChange& c = dlg.changes()[i];
        std::string row = c.label + ":  " + c.old_value + " -> " + c.new_value;
        font.draw(buf, ow, oh, sx(slab_x + 12), sy(row0_y + i * kDlgLineH),
                  row, 205, 214, 224);
    }

    // Note.
    if (has_note) {
        const std::string& note = dlg.note();
        const int note_y = row0_y + n_changes * kDlgLineH + 4;
        font.draw(buf, ow, oh,
                  sx(slab_x) + (kDlgSlabW * fw / 320 - font.measure(note)) / 2,
                  sy(note_y), note, 125, 135, 148);
    }

    // Buttons.
    const int btn_y    = slab_y + slab_h - kDlgPad - kDlgBtnH + 2;
    const int btn_w    = 80;
    const int btn_area = btn_w * 2 + kDlgBtnGap;
    const int btn0_x   = slab_x + (kDlgSlabW - btn_area) / 2;
    const int btn1_x   = btn0_x + btn_w + kDlgBtnGap;
    const std::string apply_label   = "[ Apply ]";
    const std::string discard_label = "[ Discard ]";
    const bool as = dlg.apply_selected();
    font.draw(buf, ow, oh,
              sx(btn0_x) + (btn_w * fw / 320 - font.measure(apply_label)) / 2,
              sy(btn_y + 8),
              apply_label, as ? 255 : 205, as ? 255 : 214, as ? 255 : 224);
    font.draw(buf, ow, oh,
              sx(btn1_x) + (btn_w * fw / 320 - font.measure(discard_label)) / 2,
              sy(btn_y + 8),
              discard_label, as ? 205 : 255, as ? 214 : 255, as ? 224 : 255);
    font.set_cap_px(entry_cap);
}

}  // namespace olduvai::presentation
