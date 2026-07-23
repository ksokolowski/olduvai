// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Scale-aware sprite pixel writers (native scale-1 loop + HD upscale-and-blit
// path) — the six blit_sprite / blit_sprite_keyed overloads.  Declarations
// live in game_render.hpp; moved out of game_render.cpp verbatim so that file
// stays a focused compose/foreground TU (SOC roadmap: sprite_blit).
#include "presentation/game_render.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace olduvai::presentation {

using formats::Rgb;
using formats::Sprite;

void blit_sprite(RenderTarget& t, const Sprite& s,
                 const std::vector<Rgb>& pal, int x, int y, bool flip_h) {
    // Integer entry → float core (round-trips exactly, positions < 2^24), so
    // every existing int caller stays byte-identical.
    blit_sprite(t, s, pal, static_cast<float>(x), static_cast<float>(y),
                flip_h);
}

void blit_sprite(RenderTarget& t, const Sprite& s,
                 const std::vector<Rgb>& pal, float fx, float fy, bool flip_h) {
    const auto pixels = s.decode_indexed();
    const int w = s.width, h = s.height;
    if (t.scale <= 1) {
        // Classic path — verbatim the legacy body, writing into a t.w-wide
        // buffer (t.w == 320) with t.w/t.h bounds.  Native can't show
        // sub-pixel — round to the nearest pixel.
        const int x = static_cast<int>(std::lround(fx));
        const int y = static_cast<int>(std::lround(fy));
        for (int sy = 0; sy < h; ++sy) {
            const int dy = y + sy;
            if (dy < 0 || dy >= t.h || dy >= t.clip_y) continue;
            for (int sx = 0; sx < w; ++sx) {
                const auto& p =
                    pixels[static_cast<std::size_t>(sy) * w +
                           static_cast<std::size_t>(flip_h ? (w - 1 - sx)
                                                            : sx)];
                if (!p.opaque) continue;
                const int dx = x + sx + t.origin_x;
                if (dx < 0 || dx >= t.w || dx < t.clip_x_lo || dx >= t.clip_x_hi) continue;
                const Rgb c = (p.color < pal.size())
                                  ? pal[p.color] : Rgb{255, 0, 255};
                const std::size_t off =
                    (static_cast<std::size_t>(dy) * t.w + dx) * 4;
                t.px[off] = c.r;
                t.px[off + 1] = c.g;
                t.px[off + 2] = c.b;
                t.px[off + 3] = 255;
            }
        }
        return;
    }
    // HD path: decode → RGBA (apply palette + flip) → cache upscale → blit
    // the upscaled block at scaled coordinates.
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 0);
    for (int sy = 0; sy < h; ++sy)
        for (int sx = 0; sx < w; ++sx) {
            const auto& p =
                pixels[static_cast<std::size_t>(sy) * w +
                       static_cast<std::size_t>(flip_h ? (w - 1 - sx) : sx)];
            if (!p.opaque) continue;
            const Rgb c = (p.color < pal.size())
                              ? pal[p.color] : Rgb{255, 0, 255};
            const std::size_t o = (static_cast<std::size_t>(sy) * w + sx) * 4;
            rgba[o] = c.r;
            rgba[o + 1] = c.g;
            rgba[o + 2] = c.b;
            rgba[o + 3] = 255;
        }
    const auto& hd = t.cache->get(rgba, w, h, t.scale, *t.profile);
    // Round the destination at HD resolution (see blit_sprite_keyed): a float
    // position lerped between logic ticks then advances by whole HD pixels each
    // sub-frame instead of snapping to the native grid (4-HD-px steps).
    const int ox =
        static_cast<int>(std::lround((fx + t.origin_x) * t.scale));
    const int oy = static_cast<int>(std::lround(fy * t.scale));
    for (int sy = 0; sy < hd.h; ++sy) {
        const int dy = oy + sy;
        if (dy < 0 || dy >= t.h || dy >= t.clip_y) continue;
        for (int sx = 0; sx < hd.w; ++sx) {
            const std::size_t so =
                (static_cast<std::size_t>(sy) * hd.w + sx) * 4;
            const std::uint8_t av = hd.px[so + 3];
            if (av == 0) continue;
            const int dx = ox + sx;
            if (dx < 0 || dx >= t.w || dx < t.clip_x_lo || dx >= t.clip_x_hi) continue;
            const std::size_t off =
                (static_cast<std::size_t>(dy) * t.w + dx) * 4;
            if (av == 255) {
                t.px[off] = hd.px[so];
                t.px[off + 1] = hd.px[so + 1];
                t.px[off + 2] = hd.px[so + 2];
            } else {
                // Partial-alpha edge from the blending scaler — composite over
                // the already-drawn opaque background for a smooth silhouette.
                const int ia = 255 - av;
                t.px[off] = static_cast<std::uint8_t>(
                    (hd.px[so] * av + t.px[off] * ia) / 255);
                t.px[off + 1] = static_cast<std::uint8_t>(
                    (hd.px[so + 1] * av + t.px[off + 1] * ia) / 255);
                t.px[off + 2] = static_cast<std::uint8_t>(
                    (hd.px[so + 2] * av + t.px[off + 2] * ia) / 255);
            }
            t.px[off + 3] = 255;
        }
    }
}

void blit_sprite(FrameBuffer& fb, const Sprite& s,
                 const std::vector<Rgb>& pal, int x, int y, bool flip_h) {
    RenderTarget t{fb.px.data(), fb.w, fb.h, 1, nullptr, nullptr};
    blit_sprite(t, s, pal, x, y, flip_h);
}

void blit_sprite_keyed(RenderTarget& t, const Sprite& s,
                       const std::vector<Rgb>& pal, int x, int y) {
    // Integer entry → float core.  Positions stay well within float's
    // exact-integer range (< 2^24), so std::lround round-trips the int
    // exactly: every existing int caller is byte-identical to the old path.
    blit_sprite_keyed(t, s, pal, static_cast<float>(x), static_cast<float>(y));
}

void blit_sprite_keyed(RenderTarget& t, const Sprite& s,
                       const std::vector<Rgb>& pal, float fx, float fy) {
    // Majority-vote opaque colour = background → skip those pixels.
    // Mirrors the reference's background-colour detection.
    const auto pixels = s.decode_indexed();
    const int w = s.width, h = s.height;

    // Count occurrences of each 4-bit colour index among opaque pixels.
    std::array<int, 16> counts{};
    for (const auto& p : pixels) {
        if (p.opaque && p.color < 16) {
            ++counts[p.color];
        }
    }
    // Find the colour index with the highest count.
    int bg_idx = 0, best = counts[0];
    for (int ci = 1; ci < 16; ++ci) {
        if (counts[ci] > best) { best = counts[ci]; bg_idx = ci; }
    }

    if (t.scale <= 1) {
        // Native target can't show sub-pixel — round to the nearest pixel.
        const int x = static_cast<int>(std::lround(fx));
        const int y = static_cast<int>(std::lround(fy));
        for (int sy = 0; sy < h; ++sy) {
            const int dy = y + sy;
            if (dy < 0 || dy >= t.h || dy >= t.clip_y) continue;
            for (int sx = 0; sx < w; ++sx) {
                const auto& p = pixels[static_cast<std::size_t>(sy) * w + sx];
                if (!p.opaque) continue;
                if (p.color == static_cast<std::uint8_t>(bg_idx))
                    continue;   // key out bg
                const int dx = x + sx + t.origin_x;
                if (dx < 0 || dx >= t.w || dx < t.clip_x_lo || dx >= t.clip_x_hi) continue;
                const Rgb c = (p.color < pal.size())
                                  ? pal[p.color] : Rgb{255, 0, 255};
                const std::size_t off =
                    (static_cast<std::size_t>(dy) * t.w + dx) * 4;
                t.px[off] = c.r;
                t.px[off + 1] = c.g;
                t.px[off + 2] = c.b;
                t.px[off + 3] = 255;
            }
        }
        return;
    }
    // HD path: keep the detected bg colour in RGB with alpha 0 (bg + masked
    // pixels), bubble pixels opaque.  OmniScale then blends toward the bg
    // (water) colour at edges instead of a bled opaque ring fattening the thin
    // bubble shapes — crisp, matching Python's fluid_bubble_render (set bg
    // alpha 0, keep RGB).  bleed=false: RGB is defined everywhere already.
    const Rgb bgc = (bg_idx < static_cast<int>(pal.size()))
                        ? pal[static_cast<std::size_t>(bg_idx)] : Rgb{0, 0, 0};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 0);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = bgc.r; rgba[i + 1] = bgc.g; rgba[i + 2] = bgc.b;
    }
    for (int sy = 0; sy < h; ++sy)
        for (int sx = 0; sx < w; ++sx) {
            const auto& p = pixels[static_cast<std::size_t>(sy) * w + sx];
            if (!p.opaque) continue;
            if (p.color == static_cast<std::uint8_t>(bg_idx)) continue;
            const Rgb c = (p.color < pal.size())
                              ? pal[p.color] : Rgb{255, 0, 255};
            const std::size_t o = (static_cast<std::size_t>(sy) * w + sx) * 4;
            rgba[o] = c.r;
            rgba[o + 1] = c.g;
            rgba[o + 2] = c.b;
            rgba[o + 3] = 255;
        }
    const auto& hd =
        t.cache->get(rgba, w, h, t.scale, *t.profile, /*bleed=*/false);
    // Round the destination at HD resolution, NOT native: a slow bubble rising
    // 1px/logic-frame lerps to native sub-pixels 0.0/0.33/0.67 across the three
    // 54Hz sub-frames — truncated at native those collapse to one pixel (visible
    // 18Hz stepping); at scale 4 they are 0/1.33/2.67 → distinct HD pixels, so
    // the sub-frame interpolation actually shows.  Integer callers round-trip
    // exactly (positions < 2^24), so this is byte-identical for them.
    const int ox =
        static_cast<int>(std::lround((fx + t.origin_x) * t.scale));
    const int oy = static_cast<int>(std::lround(fy * t.scale));
    for (int sy = 0; sy < hd.h; ++sy) {
        const int dy = oy + sy;
        if (dy < 0 || dy >= t.h || dy >= t.clip_y) continue;
        for (int sx = 0; sx < hd.w; ++sx) {
            const std::size_t so =
                (static_cast<std::size_t>(sy) * hd.w + sx) * 4;
            const std::uint8_t av = hd.px[so + 3];
            if (av == 0) continue;
            const int dx = ox + sx;
            if (dx < 0 || dx >= t.w || dx < t.clip_x_lo || dx >= t.clip_x_hi) continue;
            const std::size_t off =
                (static_cast<std::size_t>(dy) * t.w + dx) * 4;
            if (av == 255) {
                t.px[off] = hd.px[so];
                t.px[off + 1] = hd.px[so + 1];
                t.px[off + 2] = hd.px[so + 2];
            } else {
                // Partial-alpha edge from the blending scaler — composite over
                // the already-drawn opaque background for a smooth silhouette.
                const int ia = 255 - av;
                t.px[off] = static_cast<std::uint8_t>(
                    (hd.px[so] * av + t.px[off] * ia) / 255);
                t.px[off + 1] = static_cast<std::uint8_t>(
                    (hd.px[so + 1] * av + t.px[off + 1] * ia) / 255);
                t.px[off + 2] = static_cast<std::uint8_t>(
                    (hd.px[so + 2] * av + t.px[off + 2] * ia) / 255);
            }
            t.px[off + 3] = 255;
        }
    }
}

void blit_sprite_keyed(FrameBuffer& fb, const Sprite& s,
                       const std::vector<Rgb>& pal, int x, int y) {
    RenderTarget t{fb.px.data(), fb.w, fb.h, 1, nullptr, nullptr};
    blit_sprite_keyed(t, s, pal, x, y);
}

}  // namespace olduvai::presentation
