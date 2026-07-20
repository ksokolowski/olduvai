// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Classic integer pixel-art scalers used by the HD enhanced mode:
// nearest (block replicate), Scale2x/Scale3x (AdvanceMAME / EPX family),
// and Eagle (Dirk Stevens 1997).  All are palette-preserving (every
// output sub-pixel is COPIED from a source pixel, never blended), so
// they keep the original DOS palette intact and round transparency
// cleanly.  Ports of the reference engine's
// formats/upscale/{classical,eagle}.py.  Part of the opt-in enhanced
// path — default rendering stays native 320×200.
#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::enhance {

// Nearest-neighbour block replicate by integer `scale` (>= 1).
std::vector<std::uint8_t> nearest_scale(const std::vector<std::uint8_t>& rgba,
                                        int w, int h, int scale);

// AdvanceMAME Scale2x — RGBA in, 2W×2H RGBA out.
std::vector<std::uint8_t> scale2x(const std::vector<std::uint8_t>& rgba,
                                  int w, int h);

// AdvanceMAME Scale3x — RGBA in, 3W×3H RGBA out.
std::vector<std::uint8_t> scale3x(const std::vector<std::uint8_t>& rgba,
                                  int w, int h);

// Eagle 2x (Dirk Stevens, public domain) — RGBA in, 2W×2H RGBA out.
std::vector<std::uint8_t> eagle_2x(const std::vector<std::uint8_t>& rgba,
                                   int w, int h);

}  // namespace olduvai::enhance
