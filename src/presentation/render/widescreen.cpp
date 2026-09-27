// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen peek decision table and wide-buffer assembly.  SDL-free, so
// tests/test_widescreen.cpp builds without SDL.

#include "presentation/render/widescreen.hpp"

#include "systems/screen_topology.hpp"

#include <cstddef>

namespace olduvai::presentation {

namespace {

// Boss levels are single-arena (no horizontal neighbor to peek).  Mirrors the
// internal-id boss check in game_app.cpp (`internal == 2 || == 4 || == 6`).
bool is_boss_level(int internal_level) {
    return internal_level == 2 || internal_level == 4 || internal_level == 6;
}

}  // namespace

// All four surface levels peek (1, 3, 5, 7): the margins show the real
// adjacent screens.  3/7 have no FOND, so only their first/last edges differ.
bool level_supports_peek(int internal_level) {
    return internal_level == 1 || internal_level == 5 ||
           internal_level == 3 || internal_level == 7;
}

PeekNeighbors widescreen_neighbors(int internal_level, int current_screen,
                                   bool secret_flag, int surface_screen_count) {
    // Caves / secrets fold into current_screen >= 100; secret_flag is a
    // belt-and-braces guard for the same case.
    if (current_screen >= 100 || secret_flag) return {};
    if (is_boss_level(internal_level)) return {};
    if (!level_supports_peek(internal_level)) return {};

    // Peek only across contiguous-walk seams.  Warp/descent seams (L3 trunk
    // pocket and end descent, L7 cave hall and fake cave) come from the shared
    // topology table (systems/screen_topology.hpp, with per-seam evidence) and
    // fall back to the no-neighbour fill.
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

namespace {

constexpr int kCenterW = 320;
constexpr int kH = 200;
constexpr std::uint8_t kVoidPx[4] = {0, 0, 0, 255};   // dead-end floor

const std::uint8_t* src_px(const FrameBuffer& f, int x, int y) {
    return &f.px[(static_cast<std::size_t>(y) * kCenterW + x) * 4];
}

// Pure black = the transition seam / unfilled void (L1 surface screens have a
// black band at the far edge).
bool is_void(const std::uint8_t* p) {
    return p[0] == 0 && p[1] == 0 && p[2] == 0;
}

// The dead-end void covers only the dirt/rock floor, keeping the grass strip
// and forest above.
bool in_void_band(int y) { return y >= kH - kWideDeadendVoidRows; }

// No-neighbour margin bands for the backdrop fill: sky/mountains above the
// bottom kWideGroundBandRows, ground below.
bool in_ground_band(int y) { return y >= kH - kWideGroundBandRows; }

// The source of one margin pixel, either side, in order:
//   1. dead-end void band -> drop the dirt floor (kVoidPx);
//   2. a real neighbour on a peek row -> the neighbour's pixels;
//   3. a backdrop -> the tiling FOND, ground band mirrored (bg_extend);
//   4. surface without backdrop (dark woods, volcanic) -> the sky/tree band
//      continues from the screen's own columns, the ground band mirrors;
//   5. otherwise (boss, secret) -> mirror the edge strip, void pixels
//      extending the nearest real band.
// The left side peeks the left neighbour's right `margin` columns and mirrors
// across column 0; the right side the right neighbour's left columns, across
// column 319.
class MarginSampler {
public:
    MarginSampler(const FrameBuffer& center, const MarginFill& fill)
        : center_(center), fill_(fill) {}

    const std::uint8_t* pixel(bool dead_end, const FrameBuffer* neighbour,
                              int peek_col, int mirror_col, int tile_step,
                              int y, bool peek_row) const {
        if (dead_end && in_void_band(y)) return kVoidPx;
        if (neighbour && peek_row) return src_px(*neighbour, peek_col, y);
        if (fill_.backdrop) return bg_extend(peek_col, mirror_col, y);
        if (!fill_.reflect_pure && fill_.repeat_no_backdrop)
            return in_ground_band(y) ? src_px(center_, mirror_col, y)
                                     : src_px(center_, peek_col, y);
        return self_tile(mirror_col, tile_step, y);
    }

private:
    // Backdrop fill: the sky band wraps the tiling FOND.  The ground band
    // mirrors the near edge strip per pixel, so a textured fluid floor (icy
    // water, lava) repeats as real blocks and an empty edge (jungle, dark
    // woods) stays black instead of inventing ground; no per-row void test,
    // which painted whole rows black at any black texel.
    const std::uint8_t* bg_extend(int wcol, int mirror_col, int y) const {
        return (in_ground_band(y) && !fill_.ground_backdrop)
                   ? src_px(center_, mirror_col, y)
                   : src_px(*fill_.backdrop, wcol, y);
    }

    // Self-tile fill for one row: mirror the centre column; if that pixel is
    // void, scan toward the centre for the nearest non-void pixel, so the
    // screen's own nearest band extends instead of black.  step +1 for the
    // left margin, -1 for the right.
    const std::uint8_t* self_tile(int mirror_col, int step, int y) const {
        const std::uint8_t* p = src_px(center_, mirror_col, y);
        if (fill_.reflect_pure) return p;      // boss: black cave is real scene
        if (!is_void(p)) return p;
        for (int c = mirror_col + step; c >= 0 && c < kCenterW; c += step) {
            const std::uint8_t* q = src_px(center_, c, y);
            if (!is_void(q)) return q;
        }
        return p;   // whole row is void → black is the only honest answer
    }

    const FrameBuffer& center_;
    const MarginFill& fill_;
};

// The boss-arena darkening ramp, by distance from the outer screen edge:
// `edge` there, 1.0 at the mirror line `denom` columns in.
double edge_dim(float edge, int from_outer_edge, int denom) {
    return edge + (1.0 - edge) * (static_cast<double>(from_outer_edge) / denom);
}

}  // namespace

void compose_widescreen(std::vector<std::uint8_t>& out, int margin,
                        const FrameBuffer& center,
                        const FrameBuffer* left, const FrameBuffer* right,
                        MarginFill fill) {
    const MarginSampler sampler(center, fill);
    const int W = kCenterW + 2 * margin;
    // Margin darkening (boss arenas); denom guards margin == 1.
    const float edge = fill.margin_edge_brightness;
    const bool dim_margins = edge < 0.999f && margin > 0;
    const int dim_denom = (margin > 1) ? (margin - 1) : 1;
    out.assign(static_cast<std::size_t>(W) * kH * 4, 0);

    for (int y = 0; y < kH; ++y) {
        const bool peek_row = (y >= fill.hud_rows);
        for (int x = 0; x < W; ++x) {
            const std::uint8_t* src = nullptr;
            double dim = 1.0;
            if (x < margin) {
                src = sampler.pixel(fill.void_ground_left, left,
                                    /*peek_col=*/kCenterW - margin + x,
                                    /*mirror_col=*/margin - 1 - x,
                                    /*tile_step=*/+1, y, peek_row);
                if (dim_margins)   // x=0 outer edge → x=margin-1 mirror line
                    dim = edge_dim(edge, x, dim_denom);
            } else if (x < margin + kCenterW) {
                // Center copied verbatim (HUD included).
                src = src_px(center, x - margin, y);
            } else {
                const int r = x - margin - kCenterW;            // 0..margin-1
                src = sampler.pixel(fill.void_ground_right, right,
                                    /*peek_col=*/r,
                                    /*mirror_col=*/kCenterW - 1 - r,
                                    /*tile_step=*/-1, y, peek_row);
                if (dim_margins)   // r=0 mirror line → r=margin-1 outer edge
                    dim = edge_dim(edge, dim_denom - r, dim_denom);
            }
            std::uint8_t* dst = &out[(static_cast<std::size_t>(y) * W + x) * 4];
            dst[0] = static_cast<std::uint8_t>(src[0] * dim);
            dst[1] = static_cast<std::uint8_t>(src[1] * dim);
            dst[2] = static_cast<std::uint8_t>(src[2] * dim);
            dst[3] = 255;
        }
    }
}

}  // namespace olduvai::presentation
