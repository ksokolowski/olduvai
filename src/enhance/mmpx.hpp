// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// MMPX 2× pixel-art upscaler — "Style-Preserving Pixel Art
// Magnification", Morgan McGuire & Mara Gagiu (2021); transcribed from
// the MIT-licensed C99 reference implementation.  Part of the opt-in
// enhanced mode (default rendering stays native 320×200).

#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::enhance {

// RGBA in, 2W×2H RGBA out.
std::vector<std::uint8_t> mmpx_2x(const std::vector<std::uint8_t>& rgba,
                                  int w, int h);

}  // namespace olduvai::enhance
