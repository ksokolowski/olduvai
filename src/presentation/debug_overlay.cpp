// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/debug_overlay.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/collision_bitmap.hpp"
#include "core/types.hpp"
#include "presentation/hud_render.hpp"

namespace olduvai::presentation {

namespace {

// Alpha-blend a single RGB colour into the RGBA framebuffer at (px, py).
// Coordinates are in buffer space (already scaled).  Out-of-range = no-op.
void blend_px(FrameBuffer& fb, int px, int py, std::uint8_t r, std::uint8_t gg,
              std::uint8_t b, std::uint8_t a) {
    if (px < 0 || px >= fb.w || py < 0 || py >= fb.h) return;
    const std::size_t off =
        (static_cast<std::size_t>(py) * fb.w + px) * 4;
    const int inv = 255 - a;
    fb.px[off] = static_cast<std::uint8_t>((fb.px[off] * inv + r * a) / 255);
    fb.px[off + 1] =
        static_cast<std::uint8_t>((fb.px[off + 1] * inv + gg * a) / 255);
    fb.px[off + 2] =
        static_cast<std::uint8_t>((fb.px[off + 2] * inv + b * a) / 255);
    fb.px[off + 3] = 255;
}

// Solid (opaque) pixel set in buffer space.
void set_px(FrameBuffer& fb, int px, int py, std::uint8_t r, std::uint8_t gg,
            std::uint8_t b) {
    if (px < 0 || px >= fb.w || py < 0 || py >= fb.h) return;
    const std::size_t off =
        (static_cast<std::size_t>(py) * fb.w + px) * 4;
    fb.px[off] = r;
    fb.px[off + 1] = gg;
    fb.px[off + 2] = b;
    fb.px[off + 3] = 255;
}

// Draw a circle OUTLINE centred at native (cx, cy) with native radius `rad`.
// Coordinates are multiplied by `scale`; the ring is `scale` buffer-pixels
// thick so it survives the HD upscale.  Used to ring each entity so objects
// are unmistakably marked on the bug-report screenshots.
void draw_circle(FrameBuffer& fb, int cx, int cy, int rad, int scale,
                 std::uint8_t r, std::uint8_t gg, std::uint8_t b) {
    if (rad <= 0) return;
    const int bcx = cx * scale;
    const int bcy = cy * scale;
    const int R = rad * scale;
    const int T = scale + 1;                 // ring thickness in buffer px
    const long outer = static_cast<long>(R) * R;
    const long inner = static_cast<long>(R - T) * (R - T);
    for (int dy = -R; dy <= R; ++dy) {
        for (int dx = -R; dx <= R; ++dx) {
            const long d2 = static_cast<long>(dx) * dx +
                            static_cast<long>(dy) * dy;
            if (d2 <= outer && d2 >= inner) set_px(fb, bcx + dx, bcy + dy, r, gg, b);
        }
    }
}

}  // namespace

void draw_debug_collision(FrameBuffer& fb, const systems::SystemsState& state,
                          int scale) {
    const auto& col = state.collision;
    // Tint solid cells magenta at ~45% alpha.  Iterate native cells; fill the
    // scale*scale block for each so HD buffers are covered too.
    for (int y = 0; y < core::CollisionBitmap::kHeight; ++y) {
        for (int x = 0; x < core::CollisionBitmap::kWidth; ++x) {
            if (col.test(x, y)) continue;  // test()==true → empty/walkable
            const int bx = x * scale;
            const int by = y * scale;
            // Solid cells with a walkable cell directly above are the walk
            // surface: draw those brighter (opaque cyan) so the walk paths
            // read at a glance; interior solid stays a translucent magenta.
            const bool surface = (y > 0) && col.test(x, y - 1);
            for (int dy = 0; dy < scale; ++dy)
                for (int dx = 0; dx < scale; ++dx) {
                    if (surface)
                        blend_px(fb, bx + dx, by + dy, 0, 255, 255, 235);
                    else
                        blend_px(fb, bx + dx, by + dy, 255, 0, 255, 150);
                }
            // The DUR collision is a 1px-tall walk-surface line, invisible at
            // native res.  Thicken each surface cell downward into a cyan band
            // so the walk paths read clearly on the screenshot.
            if (surface) {
                const int band = 4 * scale;
                for (int dy = scale; dy < band; ++dy)
                    for (int dx = 0; dx < scale; ++dx)
                        blend_px(fb, bx + dx, by + dy, 0, 255, 255, 130);
            }
        }
    }
}

void draw_debug_entities(FrameBuffer& fb, const systems::SystemsState& state,
                         const std::vector<formats::Sprite>& entity_sprites,
                         int scale) {
    for (const auto& e : state.entities) {
        if (!e.active || !e.visible) continue;
        int w = 1, h = 1;
        if (e.sprite >= 0 &&
            e.sprite < static_cast<int>(entity_sprites.size())) {
            const auto& spr = entity_sprites[static_cast<std::size_t>(e.sprite)];
            w = std::max<int>(1, spr.width);
            h = std::max<int>(1, spr.height);
        }
        // Ring the entity: a bright-yellow circle centred on its draw rect,
        // radius sized to enclose the sprite (+2px breathing room).  Far more
        // legible on a screenshot than a thin box, and it reads as "object
        // here" the way the OP reference tool's circled objects did.
        const int cx = e.x + w / 2;
        const int cy = e.y + h / 2;
        const int rad = std::max(w, h) / 2 + 2;
        draw_circle(fb, cx, cy, rad, scale, 255, 240, 40);
    }
}

void draw_debug_perf(FrameBuffer& fb,
                     const std::vector<formats::Sprite>& charset,
                     const std::vector<formats::Rgb>& palette,
                     double fps, double frame_ms, int scale) {
    // draw_text targets a native 320x200 buffer; only emit at scale 1.
    if (scale != 1) return;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "FPS %4.1f  %5.2fMS",
                  fps, frame_ms);
    // Bottom-left baseline (clear of the top HUD score/timer row); palette
    // index 15 = bright.  glyph top = baseline - height.
    draw_text(fb, charset, palette, 2, 197, buf, 15);
}

}  // namespace olduvai::presentation
