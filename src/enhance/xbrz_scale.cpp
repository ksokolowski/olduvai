// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/xbrz_scale.hpp"

#include <algorithm>
#include <cstddef>

#include "enhance/parallel_rows.hpp"
#include "xbrz.h"

namespace olduvai::enhance {

namespace {
// xBRZ reads two pixels past every side of the pixel it scales.
constexpr int kReach = 2;
}  // namespace

std::vector<std::uint8_t> xbrz_scale(const std::vector<std::uint8_t>& rgba,
                                     int w, int h, int s) {
    return xbrz_scale_with(rgba, w, h, s, XbrzDistance::Platform);
}

std::vector<std::uint8_t> xbrz_scale_with(const std::vector<std::uint8_t>& rgba,
                                          int w, int h, int s, XbrzDistance path) {
    const xbrz::DistancePath dp =
        path == XbrzDistance::Reference    ? xbrz::DistancePath::Reference
        : path == XbrzDistance::AlphaTable ? xbrz::DistancePath::AlphaTable
                                           : xbrz::DEFAULT_DISTANCE_PATH;
    // xBRZ's ARGB mode counts everything outside the image as transparent, so
    // an opaque frame would get soft, partly transparent borders.  Pad by the
    // reach with the edge pixels repeated instead, scale, and crop: the border
    // then scales like any other row, as in the other profiles.  ARGB words are
    // built by shifts, which keeps the byte order on any host.
    const int pw = w + 2 * kReach, ph = h + 2 * kReach;
    std::vector<std::uint32_t> src(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph));
    parallel_rows(ph, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const int sy = std::clamp(y - kReach, 0, h - 1);
            for (int x = 0; x < pw; ++x) {
                const int sx = std::clamp(x - kReach, 0, w - 1);
                const std::size_t i = (static_cast<std::size_t>(sy) * w + sx) * 4;
                src[static_cast<std::size_t>(y) * pw + x] =
                    std::uint32_t{rgba[i + 3]} << 24 | std::uint32_t{rgba[i]} << 16 |
                    std::uint32_t{rgba[i + 1]} << 8 | rgba[i + 2];
            }
        }
    });
    const int tw = pw * s;
    std::vector<std::uint32_t> dst(static_cast<std::size_t>(tw) * static_cast<std::size_t>(ph * s));
    // xBRZ scales a band of source rows on its own: each band writes only its
    // own output rows (and its scratch space at their end) and reads the rows
    // around it, so the bands run side by side and the result is the serial one.
    parallel_rows(ph, [&](int y0, int y1) {
        xbrz::scale(static_cast<std::size_t>(s), src.data(), dst.data(), pw, ph,
                    xbrz::ScalerCfg(), y0, y1, dp);
    });

    const int ow = w * s, oh = h * s;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(ow) * static_cast<std::size_t>(oh) * 4);
    parallel_rows(oh, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < ow; ++x) {
                const std::uint32_t p =
                    dst[static_cast<std::size_t>(y + kReach * s) * tw + (x + kReach * s)];
                const std::size_t o = (static_cast<std::size_t>(y) * ow + x) * 4;
                out[o] = static_cast<std::uint8_t>(p >> 16);
                out[o + 1] = static_cast<std::uint8_t>(p >> 8);
                out[o + 2] = static_cast<std::uint8_t>(p);
                out[o + 3] = static_cast<std::uint8_t>(p >> 24);
            }
    });
    return out;
}

}  // namespace olduvai::enhance
