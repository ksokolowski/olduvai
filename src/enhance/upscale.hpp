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

// ── Cost accounting ────────────────────────────────────────────────────────
// Wall time spent INSIDE upscale_rgba, accumulated since the last reset.
//
// WHY IT LIVES HERE AND NOT AT THE CALL SITES.  There are 19 of them across 10
// files (four in boss_app, four in boss_arena, three in transition_players,
// two each in frame_presenter / widescreen_presenter / bg_compose, plus
// window_util, l3_end_level, text_screen_present and the HD asset cache).  A
// hand-kept list of 19 timing wrappers is the shape that rots the moment
// someone adds the twentieth — the same failure oracle_reach.sh had when three
// gates landed without joining its corpus list.  A counter inside the function
// cannot miss a caller.
//
// WHY IT MATTERS.  The presentation layer's present timer brackets
// FramePresenter::present, and upscale_rgba runs BEFORE that — so until
// 2026-09-06 the most expensive per-frame work in HD was invisible to
// `OLDUVAI_FRAME_STATS`, which reported `present=0.91ms` on a 17 ms frame.
// Measured after wiring this: omniscale x4 in widescreen spends ~48 ms of a
// 66 ms frame here, against a 54.9 ms budget.
//
// Cost of always-on: two steady_clock::now() calls per invocation, ~1 us per
// frame across all 19 sites against a 54.9 ms budget.  Deliberately not gated
// behind a flag — a measurement you have to remember to enable is one nobody
// takes.
struct UpscaleStats {
    double ms = 0.0;              // accumulated wall time
    unsigned long calls = 0;      // invocations (scale==1 no-ops included)
};
UpscaleStats upscale_stats();
void reset_upscale_stats();

}  // namespace olduvai::enhance
