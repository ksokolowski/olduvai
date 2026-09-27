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

// Warm the HD sprite cache at load time.  Lazily, each unseen sprite is
// upscaled in the frame that first draws it: entering the L1 secret room on a
// Cortex-A53 upscaled 56 sprites in one frame (1137.81 ms).  Warming on the
// loading screen cut that frame to 3 upscales (79.53 ms peak).  Sprites are
// too small for row splitting, so this runs sprites in parallel instead:
// key_for / build on all cores, insert single-threaded; the blit path takes no
// lock.  Both orientations are warmed (flip_h is applied before hashing).
// Returns the number of upscales; a warmed entry is skipped on a second call.
std::size_t warm_hd_sprite_cache(enhance::HdAssetCache& cache,
                                 const std::vector<formats::Sprite>& sprites,
                                 const std::vector<formats::Rgb>& pal,
                                 int scale, const std::string& profile);

}  // namespace olduvai::presentation
