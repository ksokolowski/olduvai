// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// One-call HD upscale for every presentation path (gameplay, boss, full-screen
// PC1 screens), as the reference routes all of them through one pipeline.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olduvai::enhance {

// The --hd-profile names olduvai renders; shared by the CLI validator and the
// dispatcher.  "native" is accepted (identity; HD off upstream).
const std::vector<std::string>& supported_hd_profiles();

// True iff `profile` is one of supported_hd_profiles().
bool is_supported_hd_profile(const std::string& profile);

// The profile's scaler copies whole source pixels (palette and binary alpha
// kept), so sprite transparency is re-stamped as a nearest upscale of the
// source mask.  False for blending scalers (omniscale, xbr), whose
// anti-aliased edge is kept.  Every scaler must be classified here: a missing
// palette-preserving one leaves a partial-alpha halo that no gameplay trace
// catches.  Unknown profile: false.
bool profile_preserves_palette(const std::string& profile);

// RGBA w x h in, (w*scale) x (h*scale) out, by profile:
//   native     identity (HD off upstream)
//   retro      nearest-neighbour
//   smooth     Scale2x (x2) / Scale3x (x3) / Scale2x twice (x4)
//   eagle      Eagle 2x (x2; chained for x4; x3 -> Scale3x)
//   xbr        xBR-style 2x (x2; chained for x4; x3 -> Scale3x)
//   mmpx       MMPX (x2, doubled for x4)
//   omniscale  OmniScale (x2/x3/x4)
// scale 1 returns the input.  An unknown profile throws std::invalid_argument;
// validate with is_supported_hd_profile() at startup.
std::vector<std::uint8_t> upscale_rgba(const std::vector<std::uint8_t>& px,
                                       int w, int h, int scale,
                                       const std::string& profile);

// ---- Cost accounting ----
// Wall time inside upscale_rgba since the last reset, counted here rather than
// at the ~19 call sites so no caller can be missed.  The present timer runs
// after the upscale, so without this the most expensive HD work is invisible
// (omniscale x4 widescreen: ~48 ms of a 66 ms frame).  Always on (~1 us per
// frame).
struct UpscaleStats {
    double ms = 0.0;              // accumulated wall time
    unsigned long calls = 0;      // invocations (scale==1 no-ops included)
};
UpscaleStats upscale_stats();
void reset_upscale_stats();

}  // namespace olduvai::enhance
