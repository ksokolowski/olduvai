// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// xBRF: a pixel-art upscaler that softens each block's corners along the lines
// and rounded corners its neighbours make.  Written in a clean room from a
// prose specification.

#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::enhance {

// The values xBRF decides by.  The defaults are the specification's.
struct XbrfTuning {
    double alike_below = 30.0;   // two pixels differing by less are alike
    double strong_ratio = 3.6;   // a cut is strong when one way is this many times smoother
    double slope_ratio = 2.4;    // a line rises gently or tall when one way is this many times closer
    double centre_weight = 4.0;  // weight of the middle pair in step 1's sums
    double luma_red = 0.2627;    // the luma weights; green is the rest
    double luma_blue = 0.0593;
};

// RGBA in, row by row; (w*scale)x(h*scale) RGBA out.  scale 2 to 6; 1 returns
// the input.  The picture is padded by its own edge pixels, so its border
// scales like any row inside.  Throws std::invalid_argument for another scale
// or a buffer that isn't w*h*4 long.
std::vector<std::uint8_t> xbrf_scale(const std::vector<std::uint8_t>& rgba, int w, int h,
                                     int scale);

// The same, deciding by `tuning`.
std::vector<std::uint8_t> xbrf_scale(const std::vector<std::uint8_t>& rgba, int w, int h,
                                     int scale, const XbrfTuning& tuning);

}  // namespace olduvai::enhance
