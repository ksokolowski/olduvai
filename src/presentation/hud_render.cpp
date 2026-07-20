// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/hud_render.hpp"

#include <algorithm>
#include <cstdio>

namespace olduvai::presentation {

using formats::Rgb;
using formats::Sprite;

namespace {

void fill_rect(FrameBuffer& fb, const Rgb& c, int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; ++yy) {
        if (yy < 0 || yy >= 200) continue;
        for (int xx = x; xx < x + w; ++xx) {
            if (xx < 0 || xx >= 320) continue;
            const std::size_t off =
                (static_cast<std::size_t>(yy) * 320 + xx) * 4;
            fb.px[off] = c.r;
            fb.px[off + 1] = c.g;
            fb.px[off + 2] = c.b;
            fb.px[off + 3] = 255;
        }
    }
}

Rgb pal_at(const std::vector<Rgb>& pal, std::size_t i) {
    return i < pal.size() ? pal[i] : Rgb{};
}

// 1bpp glyph at baseline y, coloured by palette index.
void draw_glyph(FrameBuffer& fb, const Sprite& g, const Rgb& c, int x,
                int baseline_y) {
    const auto pixels = g.decode_indexed();
    const int top = baseline_y - g.height;
    for (int sy = 0; sy < g.height; ++sy) {
        const int dy = top + sy;
        if (dy < 0 || dy >= 200) continue;
        for (int sx = 0; sx < g.width; ++sx) {
            if (!pixels[static_cast<std::size_t>(sy) * g.width + sx].opaque)
                continue;
            const int dx = x + sx;
            if (dx < 0 || dx >= 320) continue;
            const std::size_t off =
                (static_cast<std::size_t>(dy) * 320 + dx) * 4;
            fb.px[off] = c.r;
            fb.px[off + 1] = c.g;
            fb.px[off + 2] = c.b;
            fb.px[off + 3] = 255;
        }
    }
}

}  // namespace

void draw_text(FrameBuffer& fb, const std::vector<Sprite>& charset,
               const std::vector<Rgb>& pal, int x, int y,
               const std::string& text, int color_idx) {
    const Rgb c = pal_at(pal, static_cast<std::size_t>(color_idx));
    for (char ch : text) {
        const int idx = static_cast<unsigned char>(ch) - 0x20;
        if (idx >= 0 && idx < static_cast<int>(charset.size())) {
            const Sprite& g = charset[static_cast<std::size_t>(idx)];
            draw_glyph(fb, g, c, x, y);
            x += g.width;       // proportional advance
        } else {
            x += 8;
        }
    }
}

void draw_text_rgb(FrameBuffer& fb, const std::vector<Sprite>& charset,
                   int x, int y, const std::string& text, Rgb color) {
    for (char ch : text) {
        const int idx = static_cast<unsigned char>(ch) - 0x20;
        if (idx >= 0 && idx < static_cast<int>(charset.size())) {
            const Sprite& g = charset[static_cast<std::size_t>(idx)];
            draw_glyph(fb, g, color, x, y);
            x += g.width;
        } else {
            x += 8;
        }
    }
}

int text_width(const std::vector<Sprite>& charset, const std::string& text) {
    int w = 0;
    for (char ch : text) {
        const int idx = static_cast<unsigned char>(ch) - 0x20;
        if (idx >= 0 && idx < static_cast<int>(charset.size()))
            w += charset[static_cast<std::size_t>(idx)].width;
        else
            w += 8;
    }
    return w;
}

void draw_hud(FrameBuffer& fb, systems::SystemsState& state,
              const std::vector<Sprite>& charset,
              const std::vector<Sprite>& spr_mat,
              const std::vector<Rgb>& pal, bool hd_text) {
    if (charset.empty()) return;
    char buf[16];

    if (hd_text) {
        // Enhanced vector HUD draws over the upscaled frame; here only
        // the writebacks + the banner remain.
        const int clamped2 = std::min(state.food_count, 0x2E);
        if (clamped2 != state.food_count) state.food_count = clamped2;
        // GET READY banner sprites (read-only — the counter is advanced once
        // per LOGIC tick by the caller, NOT here: draw_hud is also called per
        // smooth-motion sub-frame, so decrementing here drained it ~Nx too fast
        // — the "GET READY flashes too briefly" bug).  In enhanced vector mode
        // these blits land on a discarded scratch (the vector banner is drawn in
        // the output overlay instead), but they're harmless and cheap.
        if (state.get_ready_counter >= 2 && state.get_ready_counter <= 17) {
            if (132 < static_cast<int>(spr_mat.size())) {
                blit_sprite(fb, spr_mat[132], pal, 0x80, 0x64);
            }
            if (133 < static_cast<int>(spr_mat.size())) {
                blit_sprite(fb, spr_mat[133], pal, 0xA2, 0x61);
            }
        }
        return;
    }

    // Score — 6 digits @ (48, 8).
    std::snprintf(buf, sizeof buf, "%06ld",
                  std::min(state.score, 999999L));
    draw_text(fb, charset, pal, 48, 8, buf);

    // Food bar @ (128, 0) with the cap-46 writeback.
    const int clamped = std::min(state.food_count, 0x2E);
    if (clamped != state.food_count) state.food_count = clamped;
    const Rgb bg = (state.current_level == 1) ? pal_at(pal, 4) : pal_at(pal, 0);
    const Rgb fill = pal_at(pal, 5);
    const Rgb border = pal_at(pal, 15);
    fill_rect(fb, bg, 128, 0, 46, 6);
    if (clamped > 0) fill_rect(fb, fill, 128, 0, clamped, 5);
    fill_rect(fb, border, 128, 0, 1, 6);     // left border
    fill_rect(fb, border, 129, 6, 45, 1);    // bottom
    fill_rect(fb, border, 174, 0, 1, 5);     // right
    for (int i = 0; i < 5; ++i) {            // cell dividers
        fill_rect(fb, border, 136 + i * 7, 3, 1, 2);
    }

    // Lives — 2 digits @ (232, 8).
    std::snprintf(buf, sizeof buf, "%02d", std::max(0, state.player.lives));
    draw_text(fb, charset, pal, 232, 8, buf);

    // Timer — 2 digits @ (296, 8); palette 5 when < 11.
    std::snprintf(buf, sizeof buf, "%02d", std::max(0, state.timer));
    draw_text(fb, charset, pal, 296, 8, buf, state.timer < 11 ? 5 : 15);

    // Energy — "ENERGY:" @ (4, 18); bg bar + pips.
    draw_text(fb, charset, pal, 4, 18, "ENERGY:");
    fill_rect(fb, pal_at(pal, 0), 64, 10, 41, 7);
    const int pips = std::min(state.player.energy, 10);
    for (int i = 0; i < pips; ++i) {
        fill_rect(fb, fill, 65 + i * 4, 11, 2, 5);
    }

    // GET READY banner — sprites 132 + 133, counter window [2, 17].  Read-only
    // draw: the counter is advanced once per LOGIC tick by the caller (see the
    // authoritative draw_hud_for_fb site in game_app.cpp), not here.
    if (state.get_ready_counter >= 2 && state.get_ready_counter <= 17) {
        if (132 < static_cast<int>(spr_mat.size())) {
            blit_sprite(fb, spr_mat[132], pal, 0x80, 0x64);
        }
        if (133 < static_cast<int>(spr_mat.size())) {
            blit_sprite(fb, spr_mat[133], pal, 0xA2, 0x61);
        }
    }
}

}  // namespace olduvai::presentation
