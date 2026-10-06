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

// A saved or typed profile name in its current spelling: "xbr", the simple edge
// blender this build no longer has, is now xBRZ.  Anything else is unchanged.
std::string canonical_hd_profile(const std::string& profile);

// The profile's scaler copies whole source pixels (palette and binary alpha
// kept), so sprite transparency is re-stamped as a nearest upscale of the
// source mask.  False for blending scalers (omniscale, xbrz), whose
// anti-aliased edge is kept.  Every scaler must be classified here: a missing
// palette-preserving one leaves a partial-alpha halo that no gameplay trace
// catches.  Unknown profile: false.
bool profile_preserves_palette(const std::string& profile);

// What upscale_rgba actually runs for this profile and scale, in words
// ("Scale3x (MMPX has no 3x form)", "MMPX, two passes"): the profile names
// describe a look, and at x3 three of them are the same scaler.
std::string describe_hd_scaler(const std::string& profile, int scale);

// The Options menu's name for a profile ("Off", "Scale2x/3x", "xBRZ"); empty
// for an unknown one.  assets/data/menus.json carries the same names, and a test
// holds the two together.
std::string hd_profile_label(const std::string& profile);

// RGBA w x h in, (w*scale) x (h*scale) out.  Each profile reaches each factor
// by the route its entry in upscale.cpp's table gives (the scaler itself, its 2x
// twice, Scale3x where it has no 3x form, or replication); describe_hd_scaler()
// words that same entry.
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
