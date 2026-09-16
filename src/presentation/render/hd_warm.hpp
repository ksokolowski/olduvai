// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pre-populate the HD sprite cache for a level's sheets, so the upscales happen
// behind the loading screen instead of in the frame that first draws them.
#pragma once
#include <string>
#include <vector>

#include "enhance/hd_asset_cache.hpp"
#include "formats/mat.hpp"
#include "formats/pc1.hpp"      // Rgb

namespace olduvai::presentation {

// WHY.  HdAssetCache upscales lazily, on the first blit of each distinct
// sprite.  On a desktop that is invisible; on a Cortex-A53 handheld it is not.
// Entering the L1 secret room presents ~56 unseen sprites at once and every one
// of them is upscaled inside that single frame — measured at 1137.81 ms for 56
// calls, which was 33% of all upscaling across a 3054-frame session and by far
// the worst remaining hitch in the port.
//
// The work itself is not wasted, and no single upscale can be made much
// cheaper: sprites are 16-32 px tall, below parallel_rows' kMinRowsToSplit, so
// each one is single-threaded BY DESIGN and splitting its rows would cost more
// in barrier than it saves.  Two things can still change.
//
// WHEN it happens.  A loading screen is a moment the player already expects to
// wait; a mid-room freeze is not.  That alone took the worst frame from 56
// upscales to 3, and upscale_peak from 1137.81 ms to 79.53 ms.
//
// HOW MANY AT ONCE.  The wrong axis for this work is rows within one sprite;
// the right one is sprites, of which there are hundreds and every one is
// independent.  That is not free to arrange, because HdAssetCache::get() fuses
// compute and insert, so threading it naively would need a lock in the
// per-frame blit path — a bad trade for a load-time problem.  The cache
// therefore exposes the phases separately (key_for/build/insert) and this pass
// runs the two expensive ones (decode+hash, then upscale) across all cores
// while the map stays strictly single-threaded.  The blit path is untouched and
// still takes no lock.
//
// Both orientations are warmed because blit_sprite applies flip_h BEFORE
// hashing, so a left-facing and a right-facing sprite are two cache entries.
//
// Returns the number of upscales performed, for the caller to log.  Cheap to
// call twice: a warmed entry is skipped before it is built.
std::size_t warm_hd_sprite_cache(enhance::HdAssetCache& cache,
                                 const std::vector<formats::Sprite>& sprites,
                                 const std::vector<formats::Rgb>& pal,
                                 int scale, const std::string& profile);

}  // namespace olduvai::presentation
