// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#pragma once
#include <cstdint>
#include <vector>

namespace olduvai::presentation {

inline constexpr int kBossHudStrip = 9;  // top HUD-strip rows (RING.PC1)

// Widescreen margin (px each side) for the boss arena.  force_env non-null ->
// clamp(atoi,0,120); else derive from the render-output aspect, capped at 120.
int boss_ws_margin(int out_w, int out_h, const char* force_env);

// Surgical HUD-pixel inpaint: copy of `bg` (RGBA w*h) with the bright HUD pixels
// in rows 0..strip-1 (all channels > 180, 1-px dilated) replaced by the pixel at
// row+strip of the same column.  Real (non-HUD) scene pixels are untouched.
std::vector<std::uint8_t> make_clean_boss_bg(const std::vector<std::uint8_t>& bg,
                                             int w = 320, int h = 200,
                                             int strip = kBossHudStrip);

}  // namespace olduvai::presentation
