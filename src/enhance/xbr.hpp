// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// xBR-style edge-preserving 2× scaler (9-pixel-neighbourhood variant) —
// port of the reference engine's edge-detection scaler.  Unlike the
// palette-preserving scalers, xBR BLENDS corner pixels along detected
// diagonals (luma-distance similarity), giving anti-aliased sprite
// silhouettes while keeping the pixel-art aesthetic.  Part of the
// opt-in enhanced path.
#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::enhance {

// xBR 2×, RGBA in → 2W×2H RGBA out.  `threshold` is the luma-distance
// similarity cutoff (reference default 32 / 255).
std::vector<std::uint8_t> xbr_2x(const std::vector<std::uint8_t>& rgba,
                                 int w, int h, int threshold = 32);

}  // namespace olduvai::enhance
