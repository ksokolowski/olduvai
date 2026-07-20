// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss_widescreen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace olduvai::presentation {

int boss_ws_margin(int out_w, int out_h, const char* force_env) {
    int m;
    if (force_env != nullptr) {
        m = std::atoi(force_env);
    } else {
        const int desired = static_cast<int>(
            std::lround(200.0 * out_w / out_h));
        m = (desired - 320) / 2;
    }
    if (m < 0) m = 0;
    if (m > 120) m = 120;
    return m;
}

std::vector<std::uint8_t> make_clean_boss_bg(const std::vector<std::uint8_t>& bg,
                                             int w, int h, int strip) {
    std::vector<std::uint8_t> clean = bg;
    if (static_cast<int>(bg.size()) < w * h * 4 || strip <= 0) return clean;
    auto bright = [&](int x, int y) {
        const std::uint8_t* p = &bg[(static_cast<std::size_t>(y) * w + x) * 4];
        return p[0] > 180 && p[1] > 180 && p[2] > 180;
    };
    std::vector<char> mask(static_cast<std::size_t>(strip) * w, 0);
    for (int y = 0; y < strip; ++y)
        for (int x = 0; x < w; ++x)
            if (bright(x, y)) mask[static_cast<std::size_t>(y) * w + x] = 1;
    std::vector<char> dil = mask;
    for (int y = 0; y < strip; ++y)
        for (int x = 0; x < w; ++x)
            if (mask[static_cast<std::size_t>(y) * w + x])
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int yy = y + dy, xx = x + dx;
                        if (yy >= 0 && yy < strip && xx >= 0 && xx < w)
                            dil[static_cast<std::size_t>(yy) * w + xx] = 1;
                    }
    for (int y = 0; y < strip; ++y)
        for (int x = 0; x < w; ++x)
            if (dil[static_cast<std::size_t>(y) * w + x]) {
                const std::size_t d = (static_cast<std::size_t>(y) * w + x) * 4;
                const std::size_t s =
                    (static_cast<std::size_t>(y + strip) * w + x) * 4;
                clean[d] = bg[s]; clean[d + 1] = bg[s + 1];
                clean[d + 2] = bg[s + 2]; clean[d + 3] = 255;
            }
    return clean;
}

}  // namespace olduvai::presentation
