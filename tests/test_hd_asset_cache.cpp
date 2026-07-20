// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include <doctest/doctest.h>

#include "enhance/hd_asset_cache.hpp"

#include <cstdint>
#include <vector>

namespace {
// A 4×4 RGBA block with a transparent border ring and an opaque centre.
std::vector<std::uint8_t> bordered() {
    std::vector<std::uint8_t> px(4 * 4 * 4, 0);
    for (int y = 1; y <= 2; ++y)
        for (int x = 1; x <= 2; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * 4 + x) * 4;
            px[i] = 200; px[i + 1] = 100; px[i + 2] = 50; px[i + 3] = 255;
        }
    return px;
}
std::vector<std::uint8_t> opaque4() {
    std::vector<std::uint8_t> px(4 * 4 * 4, 255);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i] = static_cast<std::uint8_t>(i); px[i + 1] = 80; px[i + 2] = 40;
    }
    return px;
}
}  // namespace

TEST_CASE("HdAssetCache: dimensions and identity reuse") {
    olduvai::enhance::HdAssetCache cache;
    const auto src = opaque4();
    const auto& a = cache.get(src, 4, 4, 4, "omniscale");
    CHECK(a.w == 16);
    CHECK(a.h == 16);
    CHECK(a.px.size() == 16u * 16u * 4u);
    // Same content+params returns the SAME stored object (cache hit).
    const auto& b = cache.get(src, 4, 4, 4, "omniscale");
    CHECK(&a == &b);
    // Different scale = different entry.
    const auto& c = cache.get(src, 4, 4, 2, "omniscale");
    CHECK(c.w == 8);
    CHECK(&a != &c);
}

TEST_CASE("HdAssetCache: alpha preserved, transparent border stays transparent") {
    olduvai::enhance::HdAssetCache cache;
    const auto src = bordered();
    const auto& a = cache.get(src, 4, 4, 4, "omniscale");
    // The four HD corners come from the transparent source corners: must
    // stay fully transparent (alpha 0) — no opaque bleed past the shape.
    auto alpha_at = [&](int x, int y) {
        return a.px[(static_cast<std::size_t>(y) * a.w + x) * 4 + 3];
    };
    CHECK(alpha_at(0, 0) == 0);
    CHECK(alpha_at(a.w - 1, 0) == 0);
    CHECK(alpha_at(0, a.h - 1) == 0);
    CHECK(alpha_at(a.w - 1, a.h - 1) == 0);
    // The centre stays opaque.
    CHECK(alpha_at(a.w / 2, a.h / 2) == 255);
}

TEST_CASE("HdAssetCache: scale 1 is identity passthrough") {
    olduvai::enhance::HdAssetCache cache;
    const auto src = opaque4();
    const auto& a = cache.get(src, 4, 4, 1, "omniscale");
    CHECK(a.w == 4);
    CHECK(a.px == src);
}

TEST_CASE("HdAssetCache: clear empties the store") {
    olduvai::enhance::HdAssetCache cache;
    const auto src = opaque4();
    cache.get(src, 4, 4, 2, "mmpx");
    cache.clear();
    CHECK(cache.size() == 0);
}
