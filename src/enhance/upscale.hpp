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

// True iff `profile`'s scaler copies whole source pixels (palette / binary
// alpha preserved), so a sprite's transparency must be re-stamped as a NEAREST
// upscale of the source mask.  False for blending scalers (omniscale, xbr),
// whose anti-aliased alpha edge is kept.  This is the SINGLE SOURCE OF TRUTH
// for the per-scaler alpha treatment the HD asset cache applies — adding a
// scaler forces the choice here.  It is correctness-affecting and NOT covered
// by the gameplay trace: a palette-preserving scaler omitted from this list
// silently emits a partial-alpha halo instead of the reference's crisp
// silhouette (audit A4).  Unknown profile => false (safe default: keep whatever
// alpha the pipeline already produced).
bool profile_preserves_palette(const std::string& profile);

// RGBA in (wxh), RGBA out (w*scale x h*scale).  Dispatches by profile:
//   native        identity (HD disabled upstream; returns input)
//   retro         nearest-neighbour (crisp blocky pixels)
//   smooth        Scale2x (x2) / Scale3x (x3) / Scale2x² (x4)
//   eagle         Eagle 2x (x2; chained for x4; x3 → Scale3x)
//   xbr           xBR-style 2x blend (x2; chained for x4; x3 → Scale3x)
//   mmpx          MMPX (x2, doubled for x4)
//   omniscale     OmniScale (native x2/x3/x4)
// scale 1 returns the input unchanged.  An unsupported/unimplemented
// profile name throws std::invalid_argument — callers MUST validate via
// is_supported_hd_profile() at startup so this is never reached at runtime.
std::vector<std::uint8_t> upscale_rgba(const std::vector<std::uint8_t>& px,
                                       int w, int h, int scale,
                                       const std::string& profile);

}  // namespace olduvai::enhance
