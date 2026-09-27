// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Scale-aware sprite writers: the blit_sprite / blit_sprite_keyed overloads
// (native loop + HD upscale-and-blit).  Declared in game_render.hpp.
#include "presentation/render/game_render.hpp"

#include "formats/hash64.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace olduvai::presentation {

using formats::Rgb;
using formats::Sprite;

namespace {

// The HD cache's source key for a sprite: everything its RGBA is built from
// (the planes, the size and format, the 16 palette entries a 4-bit sprite
// can index, and which builder: plain, flipped, keyed), with the scale,
// profile and bleed the asset is made at.  A hit skips the decode, the
// buffer and the pixel hash; an edited palette or another sprite is simply
// another key.
std::uint64_t sprite_source_key(const Sprite& s, const std::vector<Rgb>& pal,
                                int builder, const RenderTarget& t,
                                bool bleed) {
    formats::Hash64 k;
    k.mix(static_cast<std::uint64_t>(builder));
    k.mix(s.width);
    k.mix(s.height);
    k.mix(static_cast<std::uint64_t>(s.format));
    k.mix_bytes(s.raw_pixels.data(), s.raw_pixels.size());
    for (std::size_t i = 0; i < 16; ++i) {
        if (i < pal.size()) {
            k.mix((std::uint64_t{pal[i].r} << 16) |
                  (std::uint64_t{pal[i].g} << 8) | pal[i].b);
        } else {
            k.mix(~std::uint64_t{0});   // missing: the builders' magenta
        }
    }
    k.mix(static_cast<std::uint64_t>(t.scale));
    k.mix_str(*t.profile);
    k.mix(bleed ? 1 : 0);
    return k.value();
}

constexpr int kPlain = 0, kPlainFlipped = 1, kKeyed = 2;

// Majority-vote opaque colour = background, keyed out.  Mirrors the
// reference's background-colour detection.
int key_colour(const std::vector<formats::IndexedPixel>& pixels) {
    std::array<int, 16> counts{};
    for (const auto& p : pixels)
        if (p.opaque && p.color < 16) ++counts[p.color];
    int bg_idx = 0, best = counts[0];
    for (int ci = 1; ci < 16; ++ci) {
        if (counts[ci] > best) { best = counts[ci]; bg_idx = ci; }
    }
    return bg_idx;
}

// The keyed sprite for the HD cache: the background colour kept in RGB with
// alpha 0 and the rest opaque, so OmniScale blends edges toward the water
// colour rather than fattening thin bubbles (as the reference).  RGB is
// defined everywhere, so it is upscaled with bleed=false.
std::vector<std::uint8_t> keyed_rgba(const Sprite& s,
                                     const std::vector<Rgb>& pal) {
    const auto pixels = s.decode_indexed();
    const int bg_idx = key_colour(pixels);
    const int w = s.width, h = s.height;
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
    return rgba;
}

// Write one native palette pixel: resolve the colour, apply the dx clip, store
// opaque RGBA.  A clipped dx is a no-op.
inline void blit_pixel(RenderTarget& t, int dx, int dy,
                       const std::vector<Rgb>& pal, int color) {
    if (dx < 0 || dx >= t.w || dx < t.clip_x_lo || dx >= t.clip_x_hi) return;
    const Rgb c = (color < static_cast<int>(pal.size()))
                      ? pal[static_cast<std::size_t>(color)]
                      : Rgb{255, 0, 255};
    const std::size_t off = (static_cast<std::size_t>(dy) * t.w + dx) * 4;
    t.px[off] = c.r;
    t.px[off + 1] = c.g;
    t.px[off + 2] = c.b;
    t.px[off + 3] = 255;
}

// Blit an upscaled block at (fx, fy), clipped and alpha-composited; the HD
// half shared by both blitters (their differences are settled before `hd`
// exists).  Rounds at HD, not native: a bubble at native sub-pixels
// 0/0.33/0.67 moves to distinct HD pixels (0/1.33/2.67 at scale 4).  Integer
// positions round-trip exactly.
void blit_hd_block(RenderTarget& t, const enhance::HdAsset& hd, float fx,
                   float fy) {
    const int ox = static_cast<int>(std::lround((fx + t.origin_x) * t.scale));
    const int oy = static_cast<int>(std::lround(fy * t.scale));
    // The column range is fixed for the block (ox is constant): fold the clip
    // tests into [sx_lo, sx_hi).
    int sx_lo = 0;
    const int sx_hi = std::min(hd.w, std::min(t.w - ox, t.clip_x_hi - ox));
    if (sx_lo >= sx_hi) return;
    if (ox < 0) sx_lo = -ox;
    if (t.clip_x_lo > 0) sx_lo = std::max(sx_lo, t.clip_x_lo - ox);
    if (sx_lo >= sx_hi) return;
    for (int sy = 0; sy < hd.h; ++sy) {
        const int dy = oy + sy;
        if (dy < 0 || dy >= t.h || dy >= t.clip_y) continue;
        const std::uint8_t* sp =
            hd.px.data() + (static_cast<std::size_t>(sy) * hd.w + sx_lo) * 4;
        std::uint8_t* dp =
            t.px + (static_cast<std::size_t>(dy) * t.w + ox + sx_lo) * 4;
        const std::uint8_t* const se =
            hd.px.data() + (static_cast<std::size_t>(sy) * hd.w + sx_hi) * 4;
        for (; sp < se; sp += 4, dp += 4) {
            const std::uint8_t av = sp[3];
            if (av == 0) continue;
            if (av == 255) {
                dp[0] = sp[0];
                dp[1] = sp[1];
                dp[2] = sp[2];
            } else {
                // Partial-alpha edge from the blending scaler — composite over
                // the already-drawn opaque background for a smooth silhouette.
                const int ia = 255 - av;
                dp[0] = static_cast<std::uint8_t>((sp[0] * av + dp[0] * ia) / 255);
                dp[1] = static_cast<std::uint8_t>((sp[1] * av + dp[1] * ia) / 255);
                dp[2] = static_cast<std::uint8_t>((sp[2] * av + dp[2] * ia) / 255);
            }
            dp[3] = 255;
        }
    }
}

}  // namespace

void indexed_to_rgba(const std::vector<std::uint8_t>& pixels,
                     const std::vector<formats::Rgb>& pal, std::uint8_t* out,
                     std::size_t count) {
    count = std::min(count, pixels.size());
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t idx = pixels[i];
        const formats::Rgb c = idx < pal.size() ? pal[idx] : formats::Rgb{};
        out[i * 4] = c.r;
        out[i * 4 + 1] = c.g;
        out[i * 4 + 2] = c.b;
        out[i * 4 + 3] = 255;
    }
}

FrameBuffer pc1_frame(const formats::Pc1Image& img) {
    FrameBuffer fb;
    indexed_to_rgba(img.pixels, img.palette, fb.px.data(), 320u * 200u);
    return fb;
}

std::vector<std::uint8_t> sprite_to_rgba(const Sprite& s,
                                         const std::vector<Rgb>& pal,
                                         bool flip_h) {
    const int w = s.width, h = s.height;
    const auto pixels = s.decode_indexed();
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
    return rgba;
}

void blit_sprite(RenderTarget& t, const Sprite& s,
                 const std::vector<Rgb>& pal, int x, int y, bool flip_h) {
    // Integer entry → float core (round-trips exactly, positions < 2^24), so
    // every existing int caller stays byte-identical.
    blit_sprite(t, s, pal, static_cast<float>(x), static_cast<float>(y),
                flip_h);
}

void blit_sprite(RenderTarget& t, const Sprite& s,
                 const std::vector<Rgb>& pal, float fx, float fy, bool flip_h) {
    const int w = s.width, h = s.height;
    if (!t.hd_path()) {
        // Decoded here, not in the prologue: the HD path decodes inside
        // sprite_to_rgba() and would decode twice.
        const auto pixels = s.decode_indexed();
        // Classic path into a 320-wide buffer; native cannot show sub-pixels,
        // so round.
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
                blit_pixel(t, x + sx + t.origin_x, dy, pal, p.color);
            }
        }
        return;
    }
    // HD: sprite_to_rgba() (palette + flip), cache upscale, blit at scaled
    // coordinates.  hd_warm.cpp hashes the same bytes (game_render.hpp).
    const auto& hd = t.cache->get_by_source(
        sprite_source_key(s, pal, flip_h ? kPlainFlipped : kPlain, t, true),
        [&] { return sprite_to_rgba(s, pal, flip_h); }, w, h, t.scale,
        *t.profile);
    blit_hd_block(t, hd, fx, fy);
}

void blit_sprite(FrameBuffer& fb, const Sprite& s,
                 const std::vector<Rgb>& pal, int x, int y, bool flip_h) {
    RenderTarget t{fb.px.data(), fb.w, fb.h, 1, nullptr, nullptr};
    blit_sprite(t, s, pal, x, y, flip_h);
}

void blit_sprite_keyed(RenderTarget& t, const Sprite& s,
                       const std::vector<Rgb>& pal, int x, int y) {
    // Integer entry to the float core; positions < 2^24 round-trip exactly.
    blit_sprite_keyed(t, s, pal, static_cast<float>(x), static_cast<float>(y));
}

void blit_sprite_keyed(RenderTarget& t, const Sprite& s,
                       const std::vector<Rgb>& pal, float fx, float fy) {
    const int w = s.width, h = s.height;
    if (!t.hd_path()) {
        const auto pixels = s.decode_indexed();
        const int bg_idx = key_colour(pixels);
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
                blit_pixel(t, x + sx + t.origin_x, dy, pal, p.color);
            }
        }
        return;
    }
    const auto& hd = t.cache->get_by_source(
        sprite_source_key(s, pal, kKeyed, t, false),
        [&] { return keyed_rgba(s, pal); }, w, h, t.scale, *t.profile,
        /*bleed=*/false);
    blit_hd_block(t, hd, fx, fy);
}

void blit_sprite_keyed(FrameBuffer& fb, const Sprite& s,
                       const std::vector<Rgb>& pal, int x, int y) {
    RenderTarget t{fb.px.data(), fb.w, fb.h, 1, nullptr, nullptr};
    blit_sprite_keyed(t, s, pal, x, y);
}

}  // namespace olduvai::presentation
