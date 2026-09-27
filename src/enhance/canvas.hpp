// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The RGBA8 draw target the overlay and HD draws write into, the rectangle
// they place things with, and the one blend they all use.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "formats/pc1.hpp"   // Rgb

namespace olduvai::enhance {

// A row-major RGBA8 image, w*h*4 bytes, drawn into in place.
struct Canvas {
    std::vector<std::uint8_t>& px;
    int w;
    int h;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// `c` over the pixel at (x, y) at coverage `a` (0..255); the pixel ends
// opaque.  The caller clips.
inline void blend_pixel(const Canvas& cv, int x, int y, formats::Rgb c,
                        int a) {
    std::uint8_t* p =
        cv.px.data() + (static_cast<std::size_t>(y) * cv.w + x) * 4;
    const auto mix = [a](std::uint8_t dst, std::uint8_t src) {
        return static_cast<std::uint8_t>((src * a + dst * (255 - a)) / 255);
    };
    p[0] = mix(p[0], c.r);
    p[1] = mix(p[1], c.g);
    p[2] = mix(p[2], c.b);
    p[3] = 255;
}

// A filled rect at coverage `a`, clipped to the canvas.
inline void blend_rect(const Canvas& cv, const Rect& r, formats::Rgb c,
                       int a) {
    for (int y = std::max(0, r.y); y < std::min(cv.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(cv.w, r.x + r.w); ++x)
            blend_pixel(cv, x, y, c, a);
}

}  // namespace olduvai::enhance
