// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The output filter: how the picture meets a window it does not fill by a
// whole multiple.  "nearest" (the default) samples, so some source pixels
// come out a screen pixel wider than others; "sharp" (sharp bilinear) scales
// by the largest whole factor with nearest first, then smooths only the
// remainder, so every pixel is the same size and only its edges blend.
// SDL-free: the decision.  window_util.cpp draws it.
#pragma once

#include <algorithm>
#include <cmath>

namespace olduvai::presentation {

// The whole prescale factor for the output/source ratios `fx`, `fy`, or 0
// when plain sampling is exact (whole multiples) or the picture shrinks.
inline int sharp_prescale(double fx, double fy) {
    const double f = std::min(fx, fy);
    if (f < 1.0) return 0;
    const auto whole = [](double v) {
        return std::fabs(v - std::round(v)) < 1e-3;
    };
    if (whole(fx) && whole(fy)) return 0;
    return static_cast<int>(std::floor(f));
}

}  // namespace olduvai::presentation
