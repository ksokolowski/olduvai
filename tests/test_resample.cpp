// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// VOC upsampling: linear interpolation, not zero-order hold.  ZOH repetition
// of ~8-11 kHz 8-bit samples up to the 48 kHz device injects spectral images
// (harsh metallic grit the real SB's reconstruction filter never produced);
// the reference plays VOCs through SDL's filtered resampler.  Linear
// interpolation matches the DOSBox mixer's SB-DAC behaviour.
#include "doctest/doctest.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "presentation/resample.hpp"

using olduvai::presentation::resample_linear_u8;

TEST_CASE("silence stays silence at any ratio") {
    const std::vector<std::uint8_t> src(100, 128);   // u8 midpoint = 0
    for (const int dst_rate : {8000, 11025, 44100, 48000}) {
        const auto out = resample_linear_u8(src, 8000, dst_rate);
        for (const std::int16_t s : out) CHECK(s == 0);
    }
}

TEST_CASE("output length scales by the rate ratio") {
    const std::vector<std::uint8_t> src(1000, 128);
    CHECK(resample_linear_u8(src, 8000, 48000).size() == 6000);
    CHECK(resample_linear_u8(src, 8000, 8000).size() == 1000);
}

TEST_CASE("upsampling interpolates between neighbours (not sample-repeat)") {
    // Two source samples: min → max.  At 4x, the in-between output samples
    // must RAMP; zero-order hold would plateau at the first value.
    const std::vector<std::uint8_t> src = {0, 255};
    const auto out = resample_linear_u8(src, 8000, 32000);
    REQUIRE(out.size() == 8);
    // First sample is exactly src[0]; the next three climb strictly.
    CHECK(out[0] == static_cast<std::int16_t>((0 - 128) * 256));
    CHECK(out[1] > out[0]);
    CHECK(out[2] > out[1]);
    CHECK(out[3] > out[2]);
    // Midpoint of the ramp sits near the average of the two endpoints.
    const int lo = (0 - 128) * 256, hi = (255 - 128) << 8;
    const int mid = (lo + hi) / 2;
    CHECK(out[2] > mid - 3000);
    CHECK(out[2] < mid + 3000);
}

TEST_CASE("the final source sample is held, not read past the end") {
    const std::vector<std::uint8_t> src = {128, 255};
    const auto out = resample_linear_u8(src, 8000, 48000);
    REQUIRE(out.size() == 12);
    // The tail (past the last source point) holds the last value.
    CHECK(out.back() == static_cast<std::int16_t>((255 - 128) << 8));
}

TEST_CASE("degenerate inputs yield empty output") {
    CHECK(resample_linear_u8({}, 8000, 48000).empty());
    CHECK(resample_linear_u8({128}, 0, 48000).empty());
    CHECK(resample_linear_u8({128}, 8000, 0).empty());
}

// ── band-limited (windowed-sinc) upsampler + edge declick (A+B) ────────────

TEST_CASE("sinc: silence stays silence and DC is preserved") {
    const std::vector<std::uint8_t> quiet(200, 128);
    for (const std::int16_t s :
         olduvai::presentation::resample_sinc_u8(quiet, 4000, 48000)) {
        CHECK(s == 0);
    }
    const std::vector<std::uint8_t> dc(200, 200);   // constant non-zero
    const auto out = olduvai::presentation::resample_sinc_u8(dc, 4000, 48000);
    REQUIRE(!out.empty());
    const std::int16_t expect = (200 - 128) << 8;
    for (std::size_t i = out.size() / 4; i < out.size() * 3 / 4; ++i) {
        CHECK(out[i] >= expect - 2);   // interior: exact up to rounding
        CHECK(out[i] <= expect + 2);
    }
}

TEST_CASE("sinc output length matches the rate ratio") {
    const std::vector<std::uint8_t> src(500, 128);
    CHECK(olduvai::presentation::resample_sinc_u8(src, 4000, 48000).size() ==
          6000);
}

TEST_CASE("sinc is far smoother than linear on near-Nyquist content") {
    // 1 kHz sine at 4 kHz (4 samples per cycle) — the club-hit regime.
    std::vector<std::uint8_t> src(400);
    const int amp = 100;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const double v = std::sin(2.0 * 3.14159265358979 * i / 4.0);
        src[i] = static_cast<std::uint8_t>(128 + amp * v);
    }
    const auto lin =
        olduvai::presentation::resample_linear_u8(src, 4000, 48000);
    const auto snc =
        olduvai::presentation::resample_sinc_u8(src, 4000, 48000);
    REQUIRE(lin.size() == snc.size());
    // Imaging is spectral: a 1 kHz tone upsampled from 4 kHz mirrors at
    // 4k±1k — measure the DFT magnitude at the image bins (interior only;
    // curvature metrics are wrong here, an ideal sine curves MORE than a
    // piecewise-linear one).
    auto mag_at = [](const std::vector<std::int16_t>& v, double hz) {
        const std::size_t a = v.size() / 4, b = v.size() * 3 / 4;
        double re = 0, im = 0;
        for (std::size_t i = a; i < b; ++i) {
            const double ph = 2.0 * 3.14159265358979 * hz * i / 48000.0;
            re += v[i] * std::cos(ph);
            im += v[i] * std::sin(ph);
        }
        return std::sqrt(re * re + im * im);
    };
    const double fund_lin = mag_at(lin, 1000.0);
    const double fund_snc = mag_at(snc, 1000.0);
    // Both keep the fundamental (within 20%).
    CHECK(fund_snc > 0.8 * fund_lin);
    // The first images (3 kHz, 5 kHz) must drop by well over 10x vs linear.
    CHECK(mag_at(snc, 3000.0) < 0.1 * mag_at(lin, 3000.0));
    CHECK(mag_at(snc, 5000.0) < 0.1 * mag_at(lin, 5000.0));
}

TEST_CASE("edge fade ramps in and out") {
    std::vector<std::int16_t> pcm(1000, 10000);
    olduvai::presentation::apply_edge_fade(pcm, 48000);   // ~2 ms = 96 frames
    CHECK(pcm.front() == 0);
    CHECK(pcm.back() == 0);
    CHECK(pcm[48] > 0);
    CHECK(pcm[48] < 10000);
    CHECK(pcm[500] == 10000);   // interior untouched
    // strictly non-decreasing over the fade-in
    for (int i = 1; i < 96; ++i) CHECK(pcm[i] >= pcm[i - 1]);
}

TEST_CASE("edge fade on a tiny buffer does not explode") {
    std::vector<std::int16_t> pcm(5, 1000);
    olduvai::presentation::apply_edge_fade(pcm, 48000);
    CHECK(pcm.size() == 5);
}
