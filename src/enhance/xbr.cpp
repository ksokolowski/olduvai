// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/xbr.hpp"

#include <cstdlib>
#include <cstddef>

// xBR-style 2× scaler, 9-pixel-neighbourhood variant — port of the reference
// engine's edge-detection upscaler.  For each source pixel E with cardinal
// neighbours B (N), D (W), F (E), H (S), each output corner is a 50/50 blend
// of the two flanking neighbours when they are perceptually similar to each
// other and distinct from the perpendicular pair — otherwise it is E.  This
// turns hard pixel-art diagonals into anti-aliased ramps while keeping the
// pixel-art look (not the over-soft bilinear blur).
//
// Similarity uses ITU-R BT.601 luma distance (299/587/114 × 1000) matching
// Hyllian's reference xBR; on RGBA we treat all four channels' luma.

namespace olduvai::enhance {

namespace {

struct Px { int r, g, b, a; };

inline Px get(const std::vector<std::uint8_t>& src, int w, int h,
              int x, int y) {
    if (x < 0) x = 0; else if (x >= w) x = w - 1;
    if (y < 0) y = 0; else if (y >= h) y = h - 1;
    const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
    return {src[i], src[i + 1], src[i + 2], src[i + 3]};
}

// Luma-distance similarity, matching the reference engine's pixel-similarity test.
inline bool similar(const Px& p, const Px& q, int threshold) {
    const long y_diff =
        std::labs(static_cast<long>(p.r - q.r) * 299 +
                  static_cast<long>(p.g - q.g) * 587 +
                  static_cast<long>(p.b - q.b) * 114) /
        1000;
    return y_diff < threshold;
}

inline void put_blend(const Px& a, const Px& b, std::vector<std::uint8_t>& dst,
                      std::size_t di) {
    dst[di] = static_cast<std::uint8_t>((a.r + b.r) / 2);
    dst[di + 1] = static_cast<std::uint8_t>((a.g + b.g) / 2);
    dst[di + 2] = static_cast<std::uint8_t>((a.b + b.b) / 2);
    dst[di + 3] = static_cast<std::uint8_t>((a.a + b.a) / 2);
}

inline void put(const Px& p, std::vector<std::uint8_t>& dst, std::size_t di) {
    dst[di] = static_cast<std::uint8_t>(p.r);
    dst[di + 1] = static_cast<std::uint8_t>(p.g);
    dst[di + 2] = static_cast<std::uint8_t>(p.b);
    dst[di + 3] = static_cast<std::uint8_t>(p.a);
}

}  // namespace

std::vector<std::uint8_t> xbr_2x(const std::vector<std::uint8_t>& rgba,
                                 int w, int h, int threshold) {
    const int ow = w * 2;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(ow) * h * 2 * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Px B = get(rgba, w, h, x, y - 1);
            const Px D = get(rgba, w, h, x - 1, y);
            const Px E = get(rgba, w, h, x, y);
            const Px F = get(rgba, w, h, x + 1, y);
            const Px H = get(rgba, w, h, x, y + 1);

            const bool nw = similar(D, B, threshold) &&
                            !similar(D, F, threshold) &&
                            !similar(B, H, threshold);
            const bool ne = similar(B, F, threshold) &&
                            !similar(B, D, threshold) &&
                            !similar(F, H, threshold);
            const bool sw = similar(D, H, threshold) &&
                            !similar(D, B, threshold) &&
                            !similar(H, F, threshold);
            const bool se = similar(F, H, threshold) &&
                            !similar(F, B, threshold) &&
                            !similar(H, D, threshold);

            const std::size_t base =
                (static_cast<std::size_t>(y * 2) * ow + x * 2) * 4;
            if (nw) put_blend(D, B, out, base); else put(E, out, base);
            if (ne) put_blend(B, F, out, base + 4); else put(E, out, base + 4);
            const std::size_t row2 = base + static_cast<std::size_t>(ow) * 4;
            if (sw) put_blend(D, H, out, row2); else put(E, out, row2);
            if (se) put_blend(F, H, out, row2 + 4);
            else put(E, out, row2 + 4);
        }
    return out;
}

}  // namespace olduvai::enhance
