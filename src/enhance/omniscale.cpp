// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/omniscale.hpp"

#include <algorithm>
#include <cmath>

namespace olduvai::enhance {

namespace {

struct V4 { float r, g, b, a; };
struct V3 { float y, cr, cb; };

inline V3 to_hq(const V4& c) {
    return {0.25f * c.r + 0.25f * c.g + 0.25f * c.b,
            0.25f * c.r - 0.25f * c.b,
            -0.125f * c.r + 0.25f * c.g - 0.125f * c.b};
}

inline bool is_different(const V3& a, const V3& b) {
    return std::abs(a.y - b.y) > 0.018f || std::abs(a.cr - b.cr) > 0.002f ||
           std::abs(a.cb - b.cb) > 0.005f;
}

inline V4 mix(const V4& a, const V4& b, float t) {
    return {a.r * (1.0f - t) + b.r * t, a.g * (1.0f - t) + b.g * t,
            a.b * (1.0f - t) + b.b * t, a.a * (1.0f - t) + b.a * t};
}

inline V4 add3(const V4& a, float fa, const V4& b, float fb, const V4& c,
               float fc) {
    return {a.r * fa + b.r * fb + c.r * fc, a.g * fa + b.g * fb + c.g * fc,
            a.b * fa + b.b * fb + c.b * fc, a.a * fa + b.a * fb + c.a * fc};
}

}  // namespace

std::vector<std::uint8_t> omniscale(const std::vector<std::uint8_t>& rgba,
                                    int w, int h, int s) {
    // Float source, edge-replicated fetches.
    std::vector<V4> src(static_cast<std::size_t>(w) * h);
    std::vector<V3> hq(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        src[i] = {rgba[i * 4] / 255.0f, rgba[i * 4 + 1] / 255.0f,
                  rgba[i * 4 + 2] / 255.0f, rgba[i * 4 + 3] / 255.0f};
        hq[i] = to_hq(src[i]);
    }
    auto fat = [&](int x, int y) -> const V4& {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return src[static_cast<std::size_t>(y) * w + x];
    };
    auto hat = [&](int x, int y) -> const V3& {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return hq[static_cast<std::size_t>(y) * w + x];
    };

    const float pixel_size = std::sqrt(2.0f) / s;
    const float pixel_size5 = pixel_size * std::sqrt(5.0f);

    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * s * s *
                                  4);
    const int W = w * s;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int sy = 0; sy < s; ++sy) {
                for (int sx = 0; sx < s; ++sx) {
                    const float px0 = (sx + 0.5f) / s;
                    const float py0 = (sy + 0.5f) / s;
                    // Mirror fold.
                    const int ox = px0 > 0.5f ? -1 : 1;
                    const int oy = py0 > 0.5f ? -1 : 1;
                    const float px = px0 > 0.5f ? 1.0f - px0 : px0;
                    const float py = py0 > 0.5f ? 1.0f - py0 : py0;

                    // 3x3 fat-pixel neighbourhood (kept whole for the kernel's
                    // symmetry); the corner/edge taps w2/w5/w6/w7/w8 are unused
                    // by the current blend but retained for readability.
                    const V4& w0 = fat(x - ox, y - oy);
                    const V4& w1 = fat(x, y - oy);
                    [[maybe_unused]] const V4& w2 = fat(x + ox, y - oy);
                    const V4& w3 = fat(x - ox, y);
                    const V4& w4 = fat(x, y);
                    [[maybe_unused]] const V4& w5 = fat(x + ox, y);
                    [[maybe_unused]] const V4& w6 = fat(x - ox, y + oy);
                    [[maybe_unused]] const V4& w7 = fat(x, y + oy);
                    [[maybe_unused]] const V4& w8 = fat(x + ox, y + oy);
                    const V3& h0 = hat(x - ox, y - oy);
                    const V3& h1 = hat(x, y - oy);
                    const V3& h2 = hat(x + ox, y - oy);
                    const V3& h3 = hat(x - ox, y);
                    const V3& h4 = hat(x, y);
                    const V3& h5 = hat(x + ox, y);
                    const V3& h6 = hat(x - ox, y + oy);
                    const V3& h7 = hat(x, y + oy);
                    const V3& h8 = hat(x + ox, y + oy);

                    int pattern = 0;
                    if (is_different(h0, h4)) pattern |= 1 << 0;
                    if (is_different(h1, h4)) pattern |= 1 << 1;
                    if (is_different(h2, h4)) pattern |= 1 << 2;
                    if (is_different(h3, h4)) pattern |= 1 << 3;
                    if (is_different(h5, h4)) pattern |= 1 << 4;
                    if (is_different(h6, h4)) pattern |= 1 << 5;
                    if (is_different(h7, h4)) pattern |= 1 << 6;
                    if (is_different(h8, h4)) pattern |= 1 << 7;
                    auto P = [&](int mask, int value) {
                        return (pattern & mask) == value;
                    };
                    const bool d15 = is_different(h1, h5);
                    const bool d73 = is_different(h7, h3);
                    const bool d31 = is_different(h3, h1);
                    const bool d01 = is_different(h0, h1);
                    const bool d03 = is_different(h0, h3);

                    // The shared "diagonal r" used by several rules.
                    auto diag_r = [&]() -> V4 {
                        if (d01 || d03) return mix(w1, w3, py - px + 0.5f);
                        const V4 inner = mix(
                            add3(w1, 0.375f, w0, 0.25f, w3, 0.375f), w3,
                            py * 2.0f);
                        return mix(inner, w1, px * 2.0f);
                    };

                    V4 res;
                    // Rule chain (shader order; first match returns).
                    if ((P(0xBF, 0x37) || P(0xDB, 0x13)) && d15) {
                        res = mix(w4, w3, 0.5f - px);
                    } else if ((P(0xDB, 0x49) || P(0xEF, 0x6D)) && d73) {
                        res = mix(w4, w1, 0.5f - py);
                    } else if ((P(0x0B, 0x0B) || P(0xFE, 0x4A) ||
                                P(0xFE, 0x1A)) &&
                               d31) {
                        res = w4;
                    } else if ((P(0x6F, 0x2A) || P(0x5B, 0x0A) ||
                                P(0xBF, 0x3A) || P(0xDF, 0x5A) ||
                                P(0x9F, 0x8A) || P(0xCF, 0x8A) ||
                                P(0xEF, 0x4E) || P(0x3F, 0x0E) ||
                                P(0xFB, 0x5A) || P(0xBB, 0x8A) ||
                                P(0x7F, 0x5A) || P(0xAF, 0x8A) ||
                                P(0xEB, 0x8A)) &&
                               d31) {
                        res = mix(w4, mix(w4, w0, 0.5f - px), 0.5f - py);
                    } else if (P(0x0B, 0x08)) {
                        const V4 a = add3(w0, 0.375f, w1, 0.25f, w4, 0.375f);
                        const V4 b = mix(w4, w1, 0.5f);
                        res = mix(mix(a, b, px * 2.0f), w4, py * 2.0f);
                    } else if (P(0x0B, 0x02)) {
                        const V4 a = add3(w0, 0.375f, w3, 0.25f, w4, 0.375f);
                        const V4 b = mix(w4, w3, 0.5f);
                        res = mix(mix(a, b, py * 2.0f), w4, px * 2.0f);
                    } else if (P(0x2F, 0x2F)) {
                        const float dist = std::sqrt((px - 0.5f) * (px - 0.5f) +
                                                     (py - 0.5f) * (py - 0.5f));
                        if (dist < 0.5f - pixel_size / 2.0f) {
                            res = w4;
                        } else {
                            const V4 r = diag_r();
                            if (dist > 0.5f + pixel_size / 2.0f) {
                                res = r;
                            } else {
                                res = mix(w4, r,
                                          (dist - 0.5f + pixel_size / 2.0f) /
                                              pixel_size);
                            }
                        }
                    } else if (P(0xBF, 0x37) || P(0xDB, 0x13)) {
                        const float dist = px - 2.0f * py;
                        if (dist > pixel_size5 / 2.0f) {
                            res = w1;
                        } else {
                            const V4 r = mix(w3, w4, px + 0.5f);
                            res = dist < -pixel_size5 / 2.0f
                                      ? r
                                      : mix(r, w1,
                                            (dist + pixel_size5 / 2.0f) /
                                                pixel_size5);
                        }
                    } else if (P(0xDB, 0x49) || P(0xEF, 0x6D)) {
                        const float dist = py - 2.0f * px;
                        if (dist > pixel_size5 / 2.0f) {
                            res = w3;
                        } else {
                            const V4 r = mix(w1, w4, px + 0.5f);
                            res = dist < -pixel_size5 / 2.0f
                                      ? r
                                      : mix(r, w3,
                                            (dist + pixel_size5 / 2.0f) /
                                                pixel_size5);
                        }
                    } else if (P(0xBF, 0x8F) || P(0x7E, 0x0E)) {
                        const float dist = px + 2.0f * py;
                        if (dist > 1.0f + pixel_size5 / 2.0f) {
                            res = w4;
                        } else {
                            const V4 r = diag_r();
                            res = dist < 1.0f - pixel_size5 / 2.0f
                                      ? r
                                      : mix(r, w4,
                                            (dist + pixel_size5 / 2.0f -
                                             1.0f) / pixel_size5);
                        }
                    } else if (P(0x7E, 0x2A) || P(0xEF, 0xAB)) {
                        const float dist = py + 2.0f * px;
                        if (dist > 1.0f + pixel_size5 / 2.0f) {
                            res = w4;
                        } else {
                            const V4 r = diag_r();
                            res = dist < 1.0f - pixel_size5 / 2.0f
                                      ? r
                                      : mix(r, w4,
                                            (dist + pixel_size5 / 2.0f -
                                             1.0f) / pixel_size5);
                        }
                    } else if (P(0x1B, 0x03) || P(0x4F, 0x43) ||
                               P(0x8B, 0x83) || P(0x6B, 0x43)) {
                        res = mix(w4, w3, 0.5f - px);
                    } else if (P(0x4B, 0x09) || P(0x8B, 0x89) ||
                               P(0x1F, 0x19) || P(0x3B, 0x19)) {
                        res = mix(w4, w1, 0.5f - py);
                    } else if (P(0xFB, 0x6A) || P(0x6F, 0x6E) ||
                               P(0x3F, 0x3E) || P(0xFB, 0xFA) ||
                               P(0xDF, 0xDE) || P(0xDF, 0x1E)) {
                        res = mix(w4, w0, (1.0f - px - py) / 2.0f);
                    } else if (P(0x4F, 0x4B) || P(0x9F, 0x1B) ||
                               P(0x2F, 0x0B) || P(0xBE, 0x0A) ||
                               P(0xEE, 0x0A) || P(0x7E, 0x0A) ||
                               P(0xEB, 0x4B) || P(0x3B, 0x1B)) {
                        const float dist = px + py;
                        if (dist > 0.5f + pixel_size / 2.0f) {
                            res = w4;
                        } else {
                            const V4 r = diag_r();
                            res = dist < 0.5f - pixel_size / 2.0f
                                      ? r
                                      : mix(r, w4,
                                            (dist + pixel_size / 2.0f -
                                             0.5f) / pixel_size);
                        }
                    } else if (P(0x0B, 0x01)) {
                        const V4 a = mix(w4, w3, 0.5f - px);
                        const V4 b = mix(w1, mix(w1, w3, 0.5f), 0.5f - px);
                        res = mix(a, b, 0.5f - py);
                    } else if (P(0x0B, 0x00)) {
                        const V4 a = mix(w4, w3, 0.5f - px);
                        const V4 b = mix(w1, w0, 0.5f - px);
                        res = mix(a, b, 0.5f - py);
                    } else {
                        // Extended 7-pixel diagonal fallback.
                        const float dist = px + py;
                        if (dist > 0.5f + pixel_size / 2.0f) {
                            res = w4;
                        } else {
                            int ext = pattern;
                            if (is_different(hat(x - ox * 2, y - oy * 2), h4))
                                ext |= 1 << 8;
                            if (is_different(hat(x - ox, y - oy * 2), h4))
                                ext |= 1 << 9;
                            if (is_different(hat(x, y - oy * 2), h4))
                                ext |= 1 << 10;
                            if (is_different(hat(x + ox, y - oy * 2), h4))
                                ext |= 1 << 11;
                            if (is_different(hat(x - ox * 2, y - oy), h4))
                                ext |= 1 << 12;
                            if (is_different(hat(x - ox * 2, y), h4))
                                ext |= 1 << 13;
                            if (is_different(hat(x - ox * 2, y + oy), h4))
                                ext |= 1 << 14;
                            int popcnt = 0;
                            for (int bit = 0; bit < 15; ++bit) {
                                popcnt += (ext >> bit) & 1;
                            }
                            if (popcnt - 7 <= 0) {
                                const V4 r = mix(w1, w3, py - px + 0.5f);
                                res = dist < 0.5f - pixel_size / 2.0f
                                          ? r
                                          : mix(r, w4,
                                                (dist + pixel_size / 2.0f -
                                                 0.5f) / pixel_size);
                            } else {
                                res = w4;
                            }
                        }
                    }

                    const std::size_t o =
                        (static_cast<std::size_t>(y * s + sy) * W +
                         (x * s + sx)) * 4;
                    auto q = [](float v) {
                        return static_cast<std::uint8_t>(
                            std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
                    };
                    out[o] = q(res.r);
                    out[o + 1] = q(res.g);
                    out[o + 2] = q(res.b);
                    out[o + 3] = q(res.a);
                }
            }
        }
    }
    return out;
}

}  // namespace olduvai::enhance
