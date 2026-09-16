// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The row-band split must be BIT-IDENTICAL, not merely equivalent (§3.22).
//
// WHY THIS GATE AND NOT A SCREENSHOT.  The first attempt compared captured
// frames at different thread counts and the hashes differed — which looked
// like a threading bug and was not.  A control at a FIXED thread count also
// differed: `--play-shot-frame` is documented as not reproducible in the first
// ~30 frames (§6), so the instrument was wrong, not the subject.  This test
// has no game loop, no timing and no capture in it: one process, one buffer,
// computed both ways, compared byte for byte.
//
// It is ALWAYS-GREEN — no game files — which matters because the property it
// pins is exactly the one that would otherwise be checked by the asset-gated
// pixel goldens, and those cannot see omniscale at all (§3.20: its float
// codegen is not bit-stable across LTO relinks, so it is deliberately not
// hash-gated).  This is the only check omniscale's threading has.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "enhance/parallel_rows.hpp"
#include "enhance/upscale.hpp"

namespace {

// A source with structure the scalers actually branch on: flat runs, hard
// edges, single-pixel detail and an alpha ramp.  A uniform buffer would pass
// even if the bands were transposed.
std::vector<std::uint8_t> make_src(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            const bool edge = (x / 7 + y / 5) % 2 == 0;
            const bool dot = (x % 13 == 0) && (y % 11 == 0);
            px[i]     = static_cast<std::uint8_t>(dot ? 255 : (edge ? 32 : 200));
            px[i + 1] = static_cast<std::uint8_t>((x * 3) & 0xFF);
            px[i + 2] = static_cast<std::uint8_t>((y * 5) & 0xFF);
            px[i + 3] = static_cast<std::uint8_t>(edge ? 255 : 128);
        }
    }
    return px;
}

}  // namespace

TEST_CASE("upscale row-band threading is bit-identical to serial") {
    using namespace olduvai::enhance;
    const int w = 320, h = 200;
    const auto src = make_src(w, h);

    // Every profile the CLI accepts, at every scale it accepts, so a scaler
    // that grows a cross-row dependency later cannot slip through on the one
    // combination nobody covered.
    for (const std::string profile :
         {"retro", "smooth", "eagle", "xbr", "mmpx", "omniscale"}) {
        for (const int scale : {2, 3, 4}) {
            CAPTURE(profile);
            CAPTURE(scale);

            set_parallel_rows_enabled(false);
            const auto serial = upscale_rgba(src, w, h, scale, profile);
            set_parallel_rows_enabled(true);
            const auto threaded = upscale_rgba(src, w, h, scale, profile);
            set_parallel_rows_enabled(true);   // leave the default in place

            REQUIRE(serial.size() == threaded.size());
            CHECK(serial == threaded);
        }
    }
}

TEST_CASE("the pool reports a sane participant count") {
    // 1 is legitimate (single-core, or OLDUVAI_UPSCALE_THREADS=1); the cap is
    // 8.  A 0 or a negative would mean decide_threads() regressed.
    const int n = olduvai::enhance::parallel_row_threads();
    CHECK(n >= 1);
    CHECK(n <= 8);
}
