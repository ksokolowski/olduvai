// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OmniScale — resolution-independent pixel-art scaler (hqx-derived),
// transcribed from the MIT-licensed GLSL shader in the SameBoy project.
// Unlike MMPX it blends, trading palette preservation for smoother
// gradients.  Part of the opt-in enhanced mode.

#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::enhance {

// RGBA in, (W*s)×(H*s) RGBA out; s in {2, 3, 4}.
std::vector<std::uint8_t> omniscale(const std::vector<std::uint8_t>& rgba,
                                    int w, int h, int s);

}  // namespace olduvai::enhance
