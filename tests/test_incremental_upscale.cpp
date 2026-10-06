// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// IncrementalUpscaler (enhance/incremental_upscale.hpp): a run of frames with
// a moving sprite over a still backdrop, upscaled incrementally, must equal
// the whole-frame upscale on every frame, for every profile and scale.

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "enhance/incremental_upscale.hpp"
#include "enhance/upscale.hpp"

using olduvai::enhance::IncrementalUpscaler;
using olduvai::enhance::supported_hd_profiles;
using olduvai::enhance::upscale_rgba;

namespace {

constexpr int kW = 96, kH = 64;

// A backdrop with edges, diagonals and a few colours, so every profile's
// pattern rules fire.
std::vector<std::uint8_t> backdrop() {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kW) * kH * 4);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            std::uint8_t* p = &px[(static_cast<std::size_t>(y) * kW + x) * 4];
            const int c = ((x / 5 + y / 3) % 4) + ((x + y) % 7 == 0 ? 4 : 0);
            p[0] = static_cast<std::uint8_t>(30 * c);
            p[1] = static_cast<std::uint8_t>(200 - 20 * c);
            p[2] = static_cast<std::uint8_t>((x * y) % 3 == 0 ? 250 : 40);
            p[3] = 255;
        }
    return px;
}

// A 12x10 "sprite" with a diagonal edge and a hole, at (sx, sy).
void draw_sprite(std::vector<std::uint8_t>& px, int sx, int sy) {
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 12; ++x) {
            if (x + y < 3 || (x == 6 && y == 5)) continue;
            const int tx = sx + x, ty = sy + y;
            if (tx < 0 || ty < 0 || tx >= kW || ty >= kH) continue;
            std::uint8_t* p = &px[(static_cast<std::size_t>(ty) * kW + tx) * 4];
            p[0] = 255;
            p[1] = static_cast<std::uint8_t>(y * 20);
            p[2] = 0;
            p[3] = 255;
        }
}

}  // namespace

TEST_CASE("IncrementalUpscaler equals the whole upscale on every frame") {
    const std::vector<std::uint8_t> bg = backdrop();
    // Down the middle, then along each edge and into a corner.
    const int path[][2] = {{40, 10}, {40, 11}, {40, 14}, {41, 20}, {0, 30},
                           {-3, 31}, {86, 2},  {88, 0},  {90, 56}, {40, 10}};
    for (const std::string& profile : supported_hd_profiles()) {
        for (int scale : {2, 3, 4}) {
            IncrementalUpscaler inc;
            int frame = 0;
            for (const auto& at : path) {
                std::vector<std::uint8_t> px = bg;
                draw_sprite(px, at[0], at[1]);
                const auto& got = inc.upscale(px, kW, kH, scale, profile);
                INFO(profile, " x", scale, " frame ", frame);
                CHECK(got == upscale_rgba(px, kW, kH, scale, profile));
                ++frame;
            }
            // The run took the partial path, not only whole frames.
            if (profile != "native") CHECK(inc.counts().partial > 0);
        }
    }
}

TEST_CASE("IncrementalUpscaler reuses an unchanged frame, redoes a changed shape") {
    std::vector<std::uint8_t> px = backdrop();
    IncrementalUpscaler inc;
    inc.upscale(px, kW, kH, 2, "mmpx");
    inc.upscale(px, kW, kH, 2, "mmpx");
    CHECK(inc.counts().whole == 1);
    CHECK(inc.counts().reused == 1);
    inc.upscale(px, kW, kH, 3, "mmpx");   // another scale: whole again
    CHECK(inc.counts().whole == 2);
}

TEST_CASE("LazyUpscaler: bands on demand equal the whole upscale") {
    std::vector<std::uint8_t> px = backdrop();
    draw_sprite(px, 30, 20);
    // A window of 40 columns panning right by uneven steps, then the rest.
    const int steps[] = {0, 7, 19, 20, 33, 56};
    for (const std::string& profile : supported_hd_profiles()) {
        for (int scale : {2, 3, 4}) {
            olduvai::enhance::LazyUpscaler lazy(px, kW, kH, scale, profile);
            for (int x : steps) lazy.ensure(x, x + 40);
            lazy.ensure(0, kW);
            INFO(profile, " x", scale);
            CHECK(lazy.hd() == upscale_rgba(px, kW, kH, scale, profile));
        }
    }
}
