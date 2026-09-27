// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Whole-buffer RGBA helpers for pans and slides: clear to opaque black, and
// copy one buffer into another shifted.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace olduvai::presentation {

inline void clear_opaque(std::vector<std::uint8_t>& px) {
    std::fill(px.begin(), px.end(), 0);
    for (std::size_t i = 3; i < px.size(); i += 4) px[i] = 255;
}

// Copy `src` into `dst` (both w x h RGBA) shifted by (dx, dy), clipped.
inline void blit_shifted(std::vector<std::uint8_t>& dst,
                         const std::vector<std::uint8_t>& src, int w, int h,
                         int dx, int dy) {
    const int x0 = dx > 0 ? dx : 0;
    const int x1 = w + (dx < 0 ? dx : 0);
    if (x0 >= x1) return;
    for (int y = 0; y < h; ++y) {
        const int sy = y - dy;
        if (sy < 0 || sy >= h) continue;
        std::copy_n(src.begin() + (static_cast<std::size_t>(sy) * w +
                                   static_cast<std::size_t>(x0 - dx)) * 4,
                    static_cast<std::size_t>(x1 - x0) * 4,
                    dst.begin() + (static_cast<std::size_t>(y) * w +
                                   static_cast<std::size_t>(x0)) * 4);
    }
}

}  // namespace olduvai::presentation
