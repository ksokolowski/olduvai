// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// upscale_rgba's contract is one line — "RGBA in (wxh), RGBA out (w*scale x
// h*scale)" — and nothing enforced it.
//
// WHY THIS GATE EXISTS.  mmpx ignored scale 3 and returned a 2x buffer.  Every
// caller sizes its output from w*scale, so HdAssetCache::build set a.w/a.h to
// 3x and then wrote the alpha re-stamp past the end of a 2x vector: measured
// `malloc(): corrupted top size`, SIGABRT, core dumped.  Heap corruption from a
// scaler forgetting one branch.
//
// It was unreachable while hd_scale_for clamped to 2-or-4, and became live the
// moment scale 3 was allowed through.  That is the whole lesson: the defect was
// not in the clamp lift, it was here, waiting.  A size check is cheap and would
// have caught it the first time any scale reached any profile.
//
// ALWAYS-GREEN: synthesised input, no game files.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "enhance/upscale.hpp"

namespace {
std::vector<std::uint8_t> src(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i]     = static_cast<std::uint8_t>((i / 4) % 251);
        px[i + 1] = static_cast<std::uint8_t>((i / 8) % 241);
        px[i + 2] = static_cast<std::uint8_t>((i / 16) % 239);
        px[i + 3] = (i % 32 == 0) ? 0 : 255;   // some transparency
    }
    return px;
}
}  // namespace

TEST_CASE("upscale_rgba honours w*scale x h*scale for every profile and scale") {
    const int w = 11, h = 7;          // deliberately not multiples of any scale
    const auto in = src(w, h);
    for (const char* profile : {"native", "retro", "smooth", "eagle", "xbr",
                                "mmpx", "omniscale"}) {
        for (int scale : {1, 2, 3, 4}) {
            CAPTURE(std::string(profile));
            CAPTURE(scale);
            const auto out = olduvai::enhance::upscale_rgba(in, w, h, scale, profile);
            const std::size_t want =
                static_cast<std::size_t>(w) * scale * h * scale * 4;
            REQUIRE(out.size() == want);
        }
    }
}
