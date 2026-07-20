// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// One-call HD upscale used by every presentation path (gameplay, boss,
// intro/ending PC1 screens) — mirrors the reference engine's
// pc1_to_hd_surface routing: same pipeline for full screens as for
// in-game frames.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olduvai::enhance {

// The set of --hd-profile names olduvai actually renders.  Single source
// of truth shared by the CLI validator (src/app/main.cpp) and the dispatcher
// below — keep them in lock-step.  "native" is accepted here (identity /
// HD disabled upstream) so the CLI doesn't reject the documented name.
const std::vector<std::string>& supported_hd_profiles();

// True iff `profile` is one of supported_hd_profiles().
bool is_supported_hd_profile(const std::string& profile);

// RGBA in (w×h), RGBA out (w*scale × h*scale).  Dispatches by profile:
//   native        identity (HD disabled upstream; returns input)
//   retro         nearest-neighbour (crisp blocky pixels)
//   smooth        Scale2x (×2) / Scale3x (×3) / Scale2x² (×4)
//   eagle         Eagle 2× (×2; chained for ×4; ×3 → Scale3x)
//   xbr           xBR-style 2× blend (×2; chained for ×4; ×3 → Scale3x)
//   mmpx          MMPX (×2, doubled for ×4)
//   omniscale     OmniScale (native ×2/×3/×4)
// scale 1 returns the input unchanged.  An unsupported/unimplemented
// profile name throws std::invalid_argument — callers MUST validate via
// is_supported_hd_profile() at startup so this is never reached at runtime.
std::vector<std::uint8_t> upscale_rgba(const std::vector<std::uint8_t>& px,
                                       int w, int h, int scale,
                                       const std::string& profile);

}  // namespace olduvai::enhance
