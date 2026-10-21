// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "doctest/doctest.h"

#include "presentation/render/output_filter.hpp"

using olduvai::presentation::sharp_prescale;

TEST_CASE("output filter: a whole multiple or a shrink samples plainly") {
    CHECK(sharp_prescale(1.0, 1.0) == 0);
    CHECK(sharp_prescale(2.0, 2.0) == 0);
    CHECK(sharp_prescale(3.0, 2.0) == 0);    // whole on both axes (stretch)
    CHECK(sharp_prescale(0.8, 0.8) == 0);    // smaller than the picture
    CHECK(sharp_prescale(0.9, 1.5) == 0);
}

TEST_CASE("output filter: a fraction prescales by the whole part") {
    CHECK(sharp_prescale(1.5625, 1.5625) == 1);   // 640x400 in 1000x625
    CHECK(sharp_prescale(2.5, 2.5) == 2);
    CHECK(sharp_prescale(3.0, 3.6) == 3);         // the smaller axis decides
    CHECK(sharp_prescale(1.6875, 1.35) == 1);     // 4:3 on 1080p
}
