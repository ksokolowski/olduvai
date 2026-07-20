// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen adjacent-screen peek — pure foundation (Task 1).  See header.
//
// Both functions here are SDL-free: widescreen_neighbors is pure integer
// logic, and compose_widescreen is a plain pixel copy over FrameBuffer (a POD
// struct in game_render.hpp whose transitive includes pull no SDL).  This lets
// tests/test_widescreen.cpp compile this TU without linking SDL.

#include "presentation/widescreen.hpp"

#include "systems/screen_topology.hpp"

#include <cstddef>

namespace olduvai::presentation {

namespace {

// Boss levels are single-arena (no horizontal neighbor to peek).  Mirrors the
// internal-id boss check in game_app.cpp (`internal == 2 || == 4 || == 6`).
bool is_boss_level(int internal_level) {
    return internal_level == 2 || internal_level == 4 || internal_level == 6;
}

// Peek set: all four surface levels — 1 (jungle), 5 (icy), 3 (dark woods),
// 7 (volcanic).  Peek composes the ACTUAL horizontally-adjacent screens into
// the margins (compose_surface_screen_static), which works for tile-based
// levels (3/7) just as for the shared-FOND levels (1/5) — the neighbour is real
// level content either way.  3/7 lack a tiling FOND, so only their no-neighbour
// FIRST/LAST edges differ (handled by the caller's fallback); every interior
// screen peeks real neighbours, matching L1/L5.  (Was 1/5 only — 3/7 bezeled
// with black bars, which is what this widens.)
bool level_supports_peek(int internal_level) {
    return internal_level == 1 || internal_level == 5 ||
           internal_level == 3 || internal_level == 7;
}

}  // namespace

PeekNeighbors widescreen_neighbors(int internal_level, int current_screen,
                                   bool secret_flag, int surface_screen_count) {
    // Caves / secrets fold into current_screen >= 100; secret_flag is a
    // belt-and-braces guard for the same case.
    if (current_screen >= 100 || secret_flag) return {};
    if (is_boss_level(internal_level)) return {};
    if (!level_supports_peek(internal_level)) return {};

    // Peeks may only look across CONTIGUOUS-WALK seams.  Which seams are
    // warps / descents (L3 trunk pocket + level-end descent, L7 cave hall +
    // fake cave) is the shared topology table's business — one encoding for
    // this suppression, the transition-kind classification, and (as the
    // documented authority) systems/transitions.cpp.  Per-seam EXE evidence
    // lives at the table (systems/screen_topology.hpp, roadmap OL-B4).
    // Non-contiguous sides fall back to the no-neighbour fill, exactly as
    // the old per-level hardcoded suppressions did (verified equivalent:
    // the L3 10/11 blanket-{} case derives — both its seams are warps).
    PeekNeighbors n;
    n.left = (current_screen - 1 >= 0) ? current_screen - 1 : -1;
    n.right = (current_screen + 1 < surface_screen_count) ? current_screen + 1
                                                          : -1;
    if (n.left >= 0 &&
        !systems::seam_contiguous(internal_level, n.left, current_screen))
        n.left = -1;
    if (n.right >= 0 &&
        !systems::seam_contiguous(internal_level, current_screen, n.right))
        n.right = -1;
    return n;
}

void compose_widescreen(std::vector<std::uint8_t>& out, int margin,
                        const FrameBuffer& center,
                        const FrameBuffer* left, const FrameBuffer* right,
                        int hud_rows, const FrameBuffer* backdrop,
                        bool reflect_pure, float margin_edge_brightness,
                        bool repeat_no_backdrop, bool ground_backdrop,
                        bool void_ground_left, bool void_ground_right) {
    constexpr int kCenterW = 320;
    constexpr int kH = 200;
    static const std::uint8_t kVoidPx[4] = {0, 0, 0, 255};   // dead-end floor
    // Dead-end void covers only the chunky dirt/rock floor at the very bottom,
    // leaving the grass strip + forest above (matches screen 0's left "just
    // backdrop" look instead of a floating grass shelf over a black box).
    auto in_void_band = [&](int y) -> bool { return y >= kH - kWideDeadendVoidRows; };
    const int W = kCenterW + 2 * margin;
    // Margin darkening gradient (boss arenas): 1.0 at the mirror line (inner) →
    // margin_edge_brightness at the outer screen edge.  denom guards margin==1.
    const bool dim_margins = margin_edge_brightness < 0.999f && margin > 0;
    const int dim_denom = (margin > 1) ? (margin - 1) : 1;
    out.assign(static_cast<std::size_t>(W) * kH * 4, 0);

    auto src_px = [](const FrameBuffer& f, int x, int y) -> const std::uint8_t* {
        return &f.px[(static_cast<std::size_t>(y) * kCenterW + x) * 4];
    };
    // The no-neighbour margin is the SKY/MOUNTAIN band for all rows above the
    // bottom kWideGroundBandRows; below that it is the GROUND band.  Used only
    // by the backdrop-driven fill (legacy self_tile is row-agnostic).
    auto in_ground_band = [&](int y) -> bool {
        return y >= kH - kWideGroundBandRows;
    };
    auto is_void = [](const std::uint8_t* p) -> bool {
        // Pure-black = the screen-transition seam / unfilled void.  L1 surface
        // screens have a black column band at the far edge where the next
        // screen would continue; mirroring it self-tiles black into the
        // margin (the L1 last-screen black bar).  Treat exact (0,0,0) as void.
        return p[0] == 0 && p[1] == 0 && p[2] == 0;
    };
    // Self-tile (no neighbor) edge fill for one row.  `mirror_col` is the
    // reflected center column for this margin pixel; if that pixel is the void
    // seam, scan horizontally TOWARD the center for the nearest non-void pixel
    // and clamp it — i.e. extend the screen's own nearest real band (sky stays
    // sky, the ground/grass + terrain band extends outward) instead of tiling
    // black.  Width-independent (per-row Y is untouched), so the secret-room
    // self-tile and the level first/last edge both fill naturally.  step = +1
    // for the left margin (scan rightward, into the screen), -1 for the right
    // margin (scan leftward, into the screen).
    auto self_tile = [&](int mirror_col, int step, int y) -> const std::uint8_t* {
        const std::uint8_t* p = src_px(center, mirror_col, y);
        if (reflect_pure) return p;            // boss: black cave is real scene
        if (!is_void(p)) return p;
        for (int c = mirror_col + step; c >= 0 && c < kCenterW; c += step) {
            const std::uint8_t* q = src_px(center, c, y);
            if (!is_void(q)) return q;
        }
        return p;   // whole row is void → black is the only honest answer
    };
    // GROUND-band no-neighbour fill: continue the floor the way the area
    // ADJACENT to the level edge actually looks — copy the near edge, not the
    // far side.  MIRROR the screen's own near edge strip (`mirror_col`) so a
    // textured FLUID floor (icy water, volcanic lava) repeats as real sprite
    // blocks instead of a single smeared column.  BUT if the near EDGE column
    // (`edge_col` = 0 left / 319 right) is VOID, the floor is empty there
    // (jungle / dark woods: the solid ground starts UNDER the player, the edge
    // is empty space) → stay BLACK, so the boundary reads as empty rather than
    // inventing walkable terrain the player would be lured toward.
    // Plain PER-PIXEL mirror — NO per-row "is the near edge column void?" test:
    // that painted whole margin rows black wherever col 0/319 hit a black texel
    // (lava crevices, the dark line under the water surface), adding spurious
    // full-width black STRIPES and swallowing the icy water's surface waves.  A
    // mirror of a void region is already black, so empty edges (jungle / dark
    // woods, ground starts under the player) stay black with no extra logic.
    auto ground_fill = [&](int mirror_col, int y) -> const std::uint8_t* {
        return src_px(center, mirror_col, y);
    };
    // Background-layer extension (no-neighbour, `backdrop` provided): the sky /
    // upper band wraps the tiling FOND for seamless mountains; the bottom ground
    // band mirrors the near edge (ground_fill).
    auto bg_extend = [&](int wcol, int mirror_col,
                         int y) -> const std::uint8_t* {
        return (in_ground_band(y) && !ground_backdrop)
                   ? ground_fill(mirror_col, y)
                   : src_px(*backdrop, wcol, y);
    };

    for (int y = 0; y < kH; ++y) {
        const bool peek_row = (y >= hud_rows);
        for (int x = 0; x < W; ++x) {
            const std::uint8_t* src = nullptr;
            double dim = 1.0;
            if (x < margin) {
                // Left margin: peek the right `margin` columns of the left
                // neighbor (HUD rows excluded); NO neighbor → MIRROR the center's
                // own left edge strip (reflect across column 0) so detailed
                // terrain reads as a natural symmetric continuation instead of a
                // single smeared column.  Self-tile, no black bezel; void seam
                // pixels extend the nearest real band rightward (self_tile).
                if (void_ground_left && in_void_band(y))
                    src = kVoidPx;   // dead-end: drop the dirt floor
                else if (left && peek_row)
                    src = src_px(*left, kCenterW - margin + x, y);
                else if (backdrop)
                    src = bg_extend(kCenterW - margin + x,
                                    /*mirror*/margin - 1 - x, y);
                else if (!reflect_pure && repeat_no_backdrop)
                    // Surface no-backdrop (dark woods / volcanic): the sky /
                    // tree band CONTINUES (torus-wrap the screen's right cols);
                    // the GROUND band mirrors the near floor (lava / empty).
                    src = in_ground_band(y)
                              ? ground_fill(/*mirror*/margin - 1 - x, y)
                              : src_px(center, kCenterW - margin + x, y);
                else
                    src = self_tile(margin - 1 - x, +1, y);  // boss/secret: mirror
                if (dim_margins)   // x=0 outer edge → x=margin-1 mirror line
                    dim = margin_edge_brightness +
                          (1.0 - margin_edge_brightness) *
                              (static_cast<double>(x) / dim_denom);
            } else if (x < margin + kCenterW) {
                // Center copied verbatim (HUD included).
                src = src_px(center, x - margin, y);
            } else {
                // Right margin: peek the left `margin` columns of the right
                // neighbor (HUD rows excluded); NO neighbor → MIRROR the center's
                // own right edge strip (reflect across column 319).  Void seam
                // pixels (the L1 last-screen black band) extend the nearest real
                // band leftward instead of tiling black (self_tile).
                const int r = x - margin - kCenterW;            // 0..margin-1
                if (void_ground_right && in_void_band(y))
                    src = kVoidPx;   // dead-end: drop the dirt floor
                else if (right && peek_row)
                    src = src_px(*right, r, y);
                else if (backdrop)
                    src = bg_extend(r, /*mirror*/kCenterW - 1 - r, y);
                else if (!reflect_pure && repeat_no_backdrop)
                    src = in_ground_band(y)
                              ? ground_fill(kCenterW - 1 - r, y)
                              : src_px(center, r, y);   // sky continues, ground mirrors near
                else
                    src = self_tile(kCenterW - 1 - r, -1, y);   // boss/secret: mirror
                if (dim_margins)   // r=0 mirror line → r=margin-1 outer edge
                    dim = margin_edge_brightness +
                          (1.0 - margin_edge_brightness) *
                              (static_cast<double>(dim_denom - r) / dim_denom);
            }
            std::uint8_t* dst = &out[(static_cast<std::size_t>(y) * W + x) * 4];
            // src is always set now (neighbor peek or same-screen edge-clamp);
            // the bezel fallback is gone — null neighbor self-tiles instead.
            // dim applies the boss margin darkening gradient (1.0 elsewhere).
            dst[0] = static_cast<std::uint8_t>(src[0] * dim);
            dst[1] = static_cast<std::uint8_t>(src[1] * dim);
            dst[2] = static_cast<std::uint8_t>(src[2] * dim);
            dst[3] = 255;
        }
    }
}

}  // namespace olduvai::presentation
