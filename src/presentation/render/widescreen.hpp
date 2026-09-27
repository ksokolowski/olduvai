// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen margins: fill the space beside the 320x200 screen with a peek of
// the adjacent screens (background + terrain, no entities) or a static fill.
// Cosmetic only — the screen flip still happens at the 320 edge.

#pragma once

#include <cstdint>
#include <vector>

#include "presentation/render/game_render.hpp"  // FrameBuffer

namespace olduvai::presentation {

// Adjacent surface screens to peek into the margins; -1 = none.
struct PeekNeighbors {
    int left = -1;
    int right = -1;
};

// -1 for caves/secrets (screen >= 100), boss levels (internal 2/4/6), and the
// level's first/last surface edge.  `secret_flag` is redundant with the
// screen >= 100 test; kept to match the SystemsState gate.
PeekNeighbors widescreen_neighbors(int internal_level, int current_screen,
                                   bool secret_flag, int surface_screen_count);

// The four surface levels (internal 1/3/5/7).  Their no-neighbour screens get
// the static wide fill instead of a black pillarbox.
bool level_supports_peek(int internal_level);

// Bottom rows of the no-neighbour fill clamped from the centre's edge column
// (floor/grass); rows above sample the backdrop's edge column.  Covers the
// L1/L5 floor + grass strip.
inline constexpr int kWideGroundBandRows = 48;

// Bottom rows blacked out on a dead-end margin: the dirt/rock floor only, so
// the grass above does not become a floating shelf.
inline constexpr int kWideDeadendVoidRows = 24;

// How a margin with no neighbour is filled.
struct MarginFill {
    // A neighbour's top hud_rows are not peeked; those rows edge-clamp.
    int hud_rows = 0;
    // HUD-erased FOND (320x200 RGBA): sky/mountains for the upper band.
    // Null (secret rooms, boss arenas): mirror the centre's edge strip.
    const FrameBuffer* backdrop = nullptr;
    // Mirror without the void-scan, so black stays black (boss arena walls).
    bool reflect_pure = false;
    // Margin fades from 1.0 at the inner edge to this at the outer edge.
    float margin_edge_brightness = 1.0f;
    // No backdrop: wrap the scene (L3/L7) instead of mirroring.
    bool repeat_no_backdrop = false;
    // Fill the ground band from the backdrop too (L1 mid-air island end).
    bool ground_backdrop = false;
    // Dead-end edge (L3 screen 9 right): black the bottom
    // kWideDeadendVoidRows of that side.
    bool void_ground_left = false;
    bool void_ground_right = false;
};

// out = (320 + 2*margin) x 200 RGBA.  Centre copied verbatim; left margin =
// right columns of `left`, right margin = left columns of `right`; a null
// neighbour uses `fill`.
void compose_widescreen(std::vector<std::uint8_t>& out, int margin,
                        const FrameBuffer& center,
                        const FrameBuffer* left, const FrameBuffer* right,
                        MarginFill fill = {});

}  // namespace olduvai::presentation
