// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// smooth_fill_tick's return value: true tells TickPacer::end_tick to skip its
// sleep, so it may only be true when the vsync fill consumed the tick.
#include "doctest/doctest.h"

#include <SDL.h>

#include "presentation/render/smooth_config.hpp"
#include "presentation/render/smooth_present.hpp"

using namespace olduvai::presentation;

namespace {
// Set an explicit sub-frame count for one scope (the config key's path).
struct ExplicitSubframes {
    int saved;
    explicit ExplicitSubframes(int n) : saved(smooth_present_config().subframes) {
        smooth_present_config().subframes = n;
    }
    ~ExplicitSubframes() { smooth_present_config().subframes = saved; }
};
}  // namespace

TEST_CASE("smooth_fill_tick: stopping on the sub-frame cap is not a paced tick") {
    const ExplicitSubframes cap(2);
    SmoothPacer p{/*vsync=*/true, /*discrete_n=*/2, /*frame_ms=*/1000, 0};
    int calls = 0;
    const bool paced = smooth_fill_tick(p, [&](float, int) { ++calls; });
    CHECK(calls == 2);
    CHECK_FALSE(paced);   // 1000 ms budget unmet: the caller must still wait
}

TEST_CASE("smooth_fill_tick: a met budget is a paced tick") {
    const ExplicitSubframes cap(2);
    SmoothPacer p{/*vsync=*/true, /*discrete_n=*/2, /*frame_ms=*/1, 0};
    int calls = 0;
    const bool paced =
        smooth_fill_tick(p, [&](float, int) { ++calls; SDL_Delay(3); });
    CHECK(calls == 1);
    CHECK(paced);
}
