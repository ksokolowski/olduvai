// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/boss_widescreen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "presentation/env_num.hpp"

namespace olduvai::presentation {

int boss_ws_margin(int out_w, int out_h, const char* force_env) {
    int m;
    // A force value that is not a number falls through to the computed margin
    // rather than becoming one: atoi turned OLDUVAI_WS_FORCE_MARGIN=<typo>
    // into margin 0, i.e. "widescreen with no margins", which looks like a
    // compose bug rather than like a mistyped variable.
    if (force_env != nullptr && parse_int(force_env, m)) {
        // m set by parse_int
    } else {
        const int desired = static_cast<int>(
            std::lround(200.0 * out_w / out_h));
        m = (desired - 320) / 2;
    }
    if (m < 0) m = 0;
    if (m > 120) m = 120;
    // OLDUVAI_WS_DEBUG: what the margin was computed FROM, on the one function
    // both the boss arena and the platform presenter call.
    //
    // Reported 2026-09-10: black bars above and below the arena on a 1280x720
    // handheld panel.  The arithmetic here cannot produce them -- 1280x720
    // gives desired=356, m=18, a 356x200 canvas at 1.7780 against the panel's
    // 1.7778 -- so if bars appear, `out_w`/`out_h` are not the panel, and this
    // line is what says so.  Cheap enough to leave in: one fprintf behind an
    // env var, on a function called a handful of times per level.
    if (std::getenv("OLDUVAI_WS_DEBUG") != nullptr)
        std::fprintf(stderr,
                     "[WS] out=%dx%d (%.4f) -> margin=%d canvas=%dx200 (%.4f)"
                     "%s\n",
                     out_w, out_h,
                     out_h > 0 ? static_cast<double>(out_w) / out_h : 0.0,
                     m, 320 + 2 * m, (320.0 + 2 * m) / 200.0,
                     (force_env != nullptr) ? "  [FORCED]" : "");
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
