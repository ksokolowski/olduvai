// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The enhanced banner effects (--banner-fx): each one's defining property,
// not its floats (the shades are float math, and float codegen differs per
// platform).  Before this only the default caveman ever ran in a test.

#include "doctest/doctest.h"

#include <cstdint>
#include <cstdlib>

#include "enhance/banner_shader.hpp"

using olduvai::enhance::BannerShader;

namespace {

struct Rgb {
    int r, g, b;
};

Rgb shade(const BannerShader& s, float u, float v) {
    std::uint8_t r = 0, g = 0, b = 0;
    s.shade(u, v, s.column_term(u), r, g, b);
    return {r, g, b};
}

bool same(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

}  // namespace

TEST_CASE("banner_fx: an unknown name is caveman") {
    const BannerShader unknown("sparkle", 1.25f), cave("caveman", 1.25f);
    for (float u : {0.0f, 0.4f, 1.0f})
        for (float v : {0.0f, 0.5f, 1.0f})
            CHECK(same(shade(unknown, u, v), shade(cave, u, v)));
}

TEST_CASE("banner_fx: only fire and caveman flicker per column") {
    for (const char* fx : {"rainbow", "gold", "pulse"})
        CHECK(BannerShader(fx, 2.0f).column_term(0.3f) == 0.0f);
    for (const char* fx : {"fire", "caveman"}) {
        const BannerShader s(fx, 2.0f);
        CHECK(std::abs(s.column_term(0.3f)) <= 0.07f);
        CHECK(s.column_term(0.1f) != s.column_term(0.6f));
    }
}

TEST_CASE("banner_fx: caveman and fire run warm to dark down the letters") {
    for (const char* fx : {"caveman", "fire"}) {
        const BannerShader s(fx, 0.0f);
        const Rgb top = shade(s, 0.5f, 0.0f), bottom = shade(s, 0.5f, 1.0f);
        CHECK(top.g > bottom.g);        // ochre / yellow above, red below
        CHECK(bottom.r > bottom.g);
        CHECK(bottom.r > bottom.b);
    }
}

TEST_CASE("banner_fx: rainbow sweeps the hue across the width") {
    const BannerShader s("rainbow", 0.0f);
    const Rgb a = shade(s, 0.0f, 0.5f), b = shade(s, 1.0f / 3, 0.5f),
              c = shade(s, 2.0f / 3, 0.5f);
    CHECK_FALSE(same(a, b));
    CHECK_FALSE(same(b, c));
    CHECK(same(shade(s, 0.2f, 0.0f), shade(s, 0.2f, 1.0f)));   // not by v
    CHECK(a.r > a.g);   // hue 0 is red
    CHECK(b.g > b.r);   // a third round is green
    CHECK(c.b > c.g);   // two thirds is blue
}

TEST_CASE("banner_fx: gold's highlight band sits where the time puts it") {
    const BannerShader s("gold", 0.0f);   // band at u = 0
    const Rgb in_band = shade(s, 0.0f, 0.5f), outside = shade(s, 0.6f, 0.5f);
    CHECK(in_band.r == 255);
    CHECK(in_band.b > outside.b);
    CHECK(in_band.g > outside.g);
}

TEST_CASE("banner_fx: pulse is one grey over the whole banner") {
    const BannerShader s("pulse", 0.7f);
    const Rgb p = shade(s, 0.1f, 0.2f);
    CHECK(p.r == p.g);
    CHECK(p.g == p.b);
    CHECK(same(p, shade(s, 0.9f, 0.8f)));
    CHECK(p.r >= static_cast<int>(235 * 0.1f));
    CHECK(p.r <= 235);
}
