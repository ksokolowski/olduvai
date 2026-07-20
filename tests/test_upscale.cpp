// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include <doctest/doctest.h>

#include "enhance/upscale.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
// 4×4 RGBA test card with a diagonal edge (forces scaler smoothing).
std::vector<std::uint8_t> test_card() {
    std::vector<std::uint8_t> px(4 * 4 * 4, 0);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const std::size_t i = (y * 4 + x) * 4;
            const bool on = x > y;
            px[i] = on ? 255 : 30;
            px[i + 1] = on ? 200 : 30;
            px[i + 2] = on ? 50 : 90;
            px[i + 3] = 255;
        }
    return px;
}

// 6×6 card engineered so every implemented scaler diverges:
//   • a hard diagonal step (drives scale2x / eagle corner copies and
//     omniscale's gradient blend), AND
//   • an isolated pair of luma-CLOSE colours flanking a center whose
//     perpendicular neighbours are luma-DISTINCT, which is the exact
//     pattern that makes xBR emit a 50/50 blend (so xbr != nearest).
std::vector<std::uint8_t> rich_card() {
    std::vector<std::uint8_t> px(6 * 6 * 4, 0);
    auto set = [&](int x, int y, int r, int g, int b) {
        const std::size_t i = (y * 6 + x) * 4;
        px[i] = static_cast<std::uint8_t>(r);
        px[i + 1] = static_cast<std::uint8_t>(g);
        px[i + 2] = static_cast<std::uint8_t>(b);
        px[i + 3] = 255;
    };
    // Base: hard diagonal of two very distinct colours.
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 6; ++x) {
            const bool on = x > y;
            set(x, y, on ? 240 : 20, on ? 180 : 20, on ? 40 : 100);
        }
    // Plant an xBR-blend trigger at (2,2): N and W are luma-close to each
    // other (both ~mid-grey) while E and S keep the distinct base colours,
    // so xBR blends the NW corner and diverges from a pure nearest replicate.
    set(2, 1, 130, 130, 130);   // N
    set(1, 2, 128, 132, 134);   // W (luma-close to N, not exactly equal)
    set(2, 2, 200, 60, 60);     // E-of-center stays distinct
    return px;
}

std::vector<std::uint8_t> nearest(const std::vector<std::uint8_t>& px,
                                  int w, int h, int k) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * k * h * k * 4);
    for (int y = 0; y < h * k; ++y)
        for (int x = 0; x < w * k; ++x)
            for (int c = 0; c < 4; ++c)
                out[(static_cast<std::size_t>(y) * w * k + x) * 4 + c] =
                    px[(static_cast<std::size_t>(y / k) * w + x / k) * 4 + c];
    return out;
}
}  // namespace

TEST_CASE("upscale_rgba: scale 1 is identity") {
    const auto px = test_card();
    CHECK(olduvai::enhance::upscale_rgba(px, 4, 4, 1, "omniscale") == px);
}

TEST_CASE("upscale_rgba: omniscale x4 sizes and smooths") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 4, "omniscale");
    REQUIRE(up.size() == 16u * 16u * 4u);
    CHECK(up != nearest(px, 4, 4, 4));  // actually filtered, not replicated
}

TEST_CASE("upscale_rgba: mmpx x2 sizes") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 2, "mmpx");
    REQUIRE(up.size() == 8u * 8u * 4u);
}

TEST_CASE("upscale_rgba: mmpx x4 double-pass sizes") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 4, "mmpx");
    REQUIRE(up.size() == 16u * 16u * 4u);
}

TEST_CASE("upscale_rgba: retro x4 is pure nearest replicate") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 4, "retro");
    REQUIRE(up.size() == 16u * 16u * 4u);
    CHECK(up == nearest(px, 4, 4, 4));  // block-replicate, no smoothing
}

TEST_CASE("upscale_rgba: smooth x2 sizes and is not pure nearest") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 2, "smooth");
    REQUIRE(up.size() == 8u * 8u * 4u);
    CHECK(up != nearest(px, 4, 4, 2));  // scale2x reshapes the diagonal
}

TEST_CASE("upscale_rgba: smooth x4 sizes via chained scale2x") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 4, "smooth");
    REQUIRE(up.size() == 16u * 16u * 4u);
}

TEST_CASE("upscale_rgba: eagle x2 sizes, palette-preserving") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 2, "eagle");
    REQUIRE(up.size() == 8u * 8u * 4u);
    // Eagle only ever copies source pixels → every output pixel must be one
    // of the two source colours (no blended intermediates).
    for (std::size_t i = 0; i < up.size(); i += 4) {
        const bool on = (up[i] == 255 && up[i + 1] == 200 && up[i + 2] == 50);
        const bool off = (up[i] == 30 && up[i + 1] == 30 && up[i + 2] == 90);
        CHECK((on || off));
    }
}

TEST_CASE("upscale_rgba: eagle x4 sizes via chained eagle_2x") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 4, "eagle");
    REQUIRE(up.size() == 16u * 16u * 4u);
}

TEST_CASE("upscale_rgba: xbr x2 sizes and blends the diagonal") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 2, "xbr");
    REQUIRE(up.size() == 8u * 8u * 4u);
    CHECK(up != nearest(px, 4, 4, 2));  // blended ramp, not replication
}

TEST_CASE("upscale_rgba: xbr x4 sizes via chained xbr_2x") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 4, "xbr");
    REQUIRE(up.size() == 16u * 16u * 4u);
}

TEST_CASE("upscale_rgba: native x4 is identity-by-replication, sized") {
    const auto px = test_card();
    const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 4, "native");
    REQUIRE(up.size() == 16u * 16u * 4u);
    CHECK(up == nearest(px, 4, 4, 4));
}

TEST_CASE("upscale_rgba: implemented profiles are pairwise distinct") {
    const auto px = rich_card();
    const auto retro = olduvai::enhance::upscale_rgba(px, 6, 6, 4, "retro");
    const auto smooth = olduvai::enhance::upscale_rgba(px, 6, 6, 4, "smooth");
    const auto eagle = olduvai::enhance::upscale_rgba(px, 6, 6, 4, "eagle");
    const auto xbr = olduvai::enhance::upscale_rgba(px, 6, 6, 4, "xbr");
    const auto omni = olduvai::enhance::upscale_rgba(px, 6, 6, 4, "omniscale");
    CHECK(retro != smooth);
    CHECK(retro != eagle);
    CHECK(retro != xbr);
    CHECK(retro != omni);
    CHECK(smooth != xbr);
    CHECK(smooth != omni);
    CHECK(eagle != xbr);
    CHECK(xbr != omni);
}

TEST_CASE("upscale_rgba: unsupported profile throws (no silent fallback)") {
    const auto px = test_card();
    CHECK_THROWS_AS(olduvai::enhance::upscale_rgba(px, 4, 4, 4, "bogus"),
                    std::invalid_argument);
    CHECK_THROWS_AS(olduvai::enhance::upscale_rgba(px, 4, 4, 4, "painterly"),
                    std::invalid_argument);
}

TEST_CASE("is_supported_hd_profile matches the catalog") {
    using olduvai::enhance::is_supported_hd_profile;
    CHECK(is_supported_hd_profile("native"));
    CHECK(is_supported_hd_profile("retro"));
    CHECK(is_supported_hd_profile("smooth"));
    CHECK(is_supported_hd_profile("eagle"));
    CHECK(is_supported_hd_profile("xbr"));
    CHECK(is_supported_hd_profile("mmpx"));
    CHECK(is_supported_hd_profile("omniscale"));
    CHECK_FALSE(is_supported_hd_profile("painterly"));
    CHECK_FALSE(is_supported_hd_profile("cinematic"));
    CHECK_FALSE(is_supported_hd_profile("bogus"));
}

TEST_CASE("upscale_rgba: alpha preserved by palette scalers on transparent border") {
    // 4×4 opaque square centred in transparency — exercises alpha handling.
    std::vector<std::uint8_t> px(4 * 4 * 4, 0);
    for (int y = 1; y <= 2; ++y)
        for (int x = 1; x <= 2; ++x) {
            const std::size_t i = (y * 4 + x) * 4;
            px[i] = 200; px[i + 1] = 100; px[i + 2] = 50; px[i + 3] = 255;
        }
    for (const char* prof : {"retro", "smooth", "eagle"}) {
        const auto up = olduvai::enhance::upscale_rgba(px, 4, 4, 2, prof);
        // Corner sub-pixels of a fully-transparent source corner stay
        // transparent (no colour bleed into the halo).
        CHECK(up[3] == 0);                       // (0,0) alpha
        CHECK(up[(7 * 8 + 7) * 4 + 3] == 0);     // (7,7) alpha
    }
}
