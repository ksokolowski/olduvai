// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Linear-interpolation upsampler for the 8-bit VOC SFX.  Zero-order hold
// (sample repetition) injects spectral images of the ~8-11 kHz source all
// the way up the device band — an artificial harshness neither the real
// Sound Blaster (DAC + analog reconstruction filter) nor the reference
// (SDL's filtered resampler via pygame.mixer.Sound) produces.  Linear
// interpolation is the DOSBox mixer's SB-DAC behaviour: era-authentic
// 8-bit character without the added grit.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace olduvai::presentation {

// u8 (unsigned, 128 = silence) at src_rate → s16 at dst_rate.
inline std::vector<std::int16_t> resample_linear_u8(
    const std::vector<std::uint8_t>& src, int src_rate, int dst_rate) {
    if (src.empty() || src_rate <= 0 || dst_rate <= 0) return {};
    const std::size_t out_len =
        src.size() * static_cast<std::size_t>(dst_rate) /
        static_cast<std::size_t>(src_rate);
    std::vector<std::int16_t> out(out_len);
    for (std::size_t i = 0; i < out_len; ++i) {
        // Source position in 32.32 fixed point avoids float drift and stays
        // exact for the rational rate ratio.
        const std::uint64_t pos =
            (static_cast<std::uint64_t>(i) * static_cast<std::uint64_t>(src_rate)
             << 32) /
            static_cast<std::uint64_t>(dst_rate);
        const std::size_t i0 =
            std::min(static_cast<std::size_t>(pos >> 32), src.size() - 1);
        const std::size_t i1 = std::min(i0 + 1, src.size() - 1);
        const std::int32_t frac =
            static_cast<std::int32_t>((pos & 0xFFFFFFFFu) >> 17);  // 15-bit
        // * 256, not << 8: the operand is negative below the 0x80 midpoint and
        // left-shifting a negative value is UB in C++17 (same class as the
        // test_resample fix; caught by the OLDUVAI_SANITIZE lane).
        const std::int32_t a = (static_cast<std::int32_t>(src[i0]) - 128) * 256;
        const std::int32_t b = (static_cast<std::int32_t>(src[i1]) - 128) * 256;
        out[i] = static_cast<std::int16_t>(a + (((b - a) * frac) >> 15));
    }
    return out;
}

// Band-limited (windowed-sinc) upsampler — the proper conversion for the
// VOC effects.  Their content presses right against the source Nyquist
// (mean|dx|/mean|x| up to 0.95 on the 4 kHz spring), so linear interpolation
// leaves audible spectral images in the 2-6 kHz band; a Blackman-windowed
// sinc kernel (cutoff = source Nyquist) removes them.  The reference plays
// VOCs through SDL's filtered resampler, so this is parity, not enhancement.
// Upsampling only; falls back to linear when dst < src.
inline std::vector<std::int16_t> resample_sinc_u8(
    const std::vector<std::uint8_t>& src, int src_rate, int dst_rate) {
    if (src.empty() || src_rate <= 0 || dst_rate <= 0) return {};
    if (dst_rate < src_rate) {
        return resample_linear_u8(src, src_rate, dst_rate);
    }
    constexpr int kTaps = 16;   // zero-crossings per side
    constexpr double kPi = 3.14159265358979323846;
    const std::size_t out_len =
        src.size() * static_cast<std::size_t>(dst_rate) /
        static_cast<std::size_t>(src_rate);
    std::vector<std::int16_t> out(out_len);
    const auto sample = [&](std::ptrdiff_t k) -> double {
        // Clamp-to-edge, matching the linear resampler's end-hold.
        if (k < 0) k = 0;
        const std::size_t kk = std::min(static_cast<std::size_t>(k),
                                        src.size() - 1);
        // * 256, not << 8 — shift of a negative operand is UB (see above).
        return static_cast<double>((static_cast<int>(src[kk]) - 128) * 256);
    };
    for (std::size_t i = 0; i < out_len; ++i) {
        const double pos = static_cast<double>(i) * src_rate / dst_rate;
        const std::ptrdiff_t centre = static_cast<std::ptrdiff_t>(pos);
        double acc = 0.0, norm = 0.0;
        for (std::ptrdiff_t k = centre - kTaps + 1; k <= centre + kTaps;
             ++k) {
            const double x = pos - static_cast<double>(k);
            const double ax = x < 0 ? -x : x;
            if (ax >= kTaps) continue;
            const double s =
                x == 0.0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            // Blackman window over [-kTaps, kTaps].
            const double w = 0.42 + 0.5 * std::cos(kPi * ax / kTaps) +
                             0.08 * std::cos(2.0 * kPi * ax / kTaps);
            const double c = s * w;
            acc += c * sample(k);
            norm += c;
        }
        // Normalising by the coefficient sum preserves DC exactly and keeps
        // the edges flat despite the clamped kernel.
        double v = norm != 0.0 ? acc / norm : 0.0;
        if (v > 32767.0) v = 32767.0;
        if (v < -32768.0) v = -32768.0;
        out[i] = static_cast<std::int16_t>(std::lrint(v));
    }
    return out;
}

// Edge declick: ~2 ms linear fade-in/out applied once at load.  Every VOC in
// the game opens ~28% of full scale above the midpoint, so a hard start pops
// on every trigger; the real SB's analog output stage smoothed that step —
// a short ramp is the conservative digital stand-in.
inline void apply_edge_fade(std::vector<std::int16_t>& pcm, int rate,
                            int fade_ms = 2) {
    if (pcm.empty() || rate <= 0 || fade_ms <= 0) return;
    std::size_t n = static_cast<std::size_t>(rate) *
                    static_cast<std::size_t>(fade_ms) / 1000u;
    n = std::min(n, pcm.size() / 2);
    if (n == 0) return;
    for (std::size_t i = 0; i < n; ++i) {
        const double g = static_cast<double>(i) / n;
        pcm[i] = static_cast<std::int16_t>(pcm[i] * g);
        pcm[pcm.size() - 1 - i] =
            static_cast<std::int16_t>(pcm[pcm.size() - 1 - i] * g);
    }
}

// s16 variant — same linear interpolation, used for the HD SFX bake WAVs
// (already 16-bit) when the bake rate differs from the device rate.
inline std::vector<std::int16_t> resample_linear_s16(
    const std::vector<std::int16_t>& src, int src_rate, int dst_rate) {
    if (src.empty() || src_rate <= 0 || dst_rate <= 0) return {};
    if (src_rate == dst_rate) return src;
    const std::size_t out_len =
        src.size() * static_cast<std::size_t>(dst_rate) /
        static_cast<std::size_t>(src_rate);
    std::vector<std::int16_t> out(out_len);
    for (std::size_t i = 0; i < out_len; ++i) {
        const std::uint64_t pos =
            (static_cast<std::uint64_t>(i) * static_cast<std::uint64_t>(src_rate)
             << 32) /
            static_cast<std::uint64_t>(dst_rate);
        const std::size_t i0 =
            std::min(static_cast<std::size_t>(pos >> 32), src.size() - 1);
        const std::size_t i1 = std::min(i0 + 1, src.size() - 1);
        const std::int32_t frac =
            static_cast<std::int32_t>((pos & 0xFFFFFFFFu) >> 17);  // 15-bit
        const std::int32_t a = src[i0];
        const std::int32_t b = src[i1];
        out[i] = static_cast<std::int16_t>(a + (((b - a) * frac) >> 15));
    }
    return out;
}

}  // namespace olduvai::presentation
