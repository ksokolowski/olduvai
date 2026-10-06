// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Static (entity-free) background layer: base fill, backdrop extension, floor
// tiles, the secret-screen clip, and the per-screen and widescreen HD
// background caches.  Public entry points are in game_render.hpp.
#include "formats/hash64.hpp"
#include "presentation/render/game_render.hpp"

#include "presentation/render/tile_patterns.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>

#include "core/game_tables.hpp"
#include "enhance/upscale.hpp"
#include "presentation/render/edge_margin_policy.hpp"
#include "presentation/render/widescreen.hpp"
#include "systems/cave_logic.hpp"

namespace olduvai::presentation {

using core::Entity;
using core::ObjType;
using formats::Rgb;

namespace {
// Blit a full-frame 320x200 RGBA source.  scale 1: direct pixel write; scale
// > 1: through the asset cache.  `keyed`: skip alpha-0 pixels (HUD strip);
// otherwise opaque (background image).
void blit_full_rgba(RenderTarget& t, const std::vector<std::uint8_t>& src,
                    int sw, int sh, bool keyed) {
    if (!t.hd_path()) {
        for (int y = 0; y < sh; ++y)
            for (int x = 0; x < sw; ++x) {
                if (y >= t.h || x >= t.w) continue;   // same guard as HD path
                const std::size_t so = (static_cast<std::size_t>(y) * sw + x) * 4;
                if (keyed && src[so + 3] == 0) continue;
                const std::size_t o = (static_cast<std::size_t>(y) * t.w + x) * 4;
                t.px[o] = src[so]; t.px[o + 1] = src[so + 1];
                t.px[o + 2] = src[so + 2]; t.px[o + 3] = 255;
            }
        return;
    }
    const auto& hd = t.cache->get(src, sw, sh, t.scale, *t.profile);
    for (int y = 0; y < hd.h; ++y)
        for (int x = 0; x < hd.w; ++x) {
            const std::size_t so = (static_cast<std::size_t>(y) * hd.w + x) * 4;
            const std::uint8_t av = hd.px[so + 3];
            if (keyed && av == 0) continue;
            if (y >= t.h || x >= t.w) continue;
            const std::size_t o = (static_cast<std::size_t>(y) * t.w + x) * 4;
            if (!keyed || av == 255) {
                t.px[o] = hd.px[so]; t.px[o + 1] = hd.px[so + 1];
                t.px[o + 2] = hd.px[so + 2];
            } else {
                const int ia = 255 - av;
                t.px[o] = static_cast<std::uint8_t>(
                    (hd.px[so] * av + t.px[o] * ia) / 255);
                t.px[o + 1] = static_cast<std::uint8_t>(
                    (hd.px[so + 1] * av + t.px[o + 1] * ia) / 255);
                t.px[o + 2] = static_cast<std::uint8_t>(
                    (hd.px[so + 2] * av + t.px[o + 2] * ia) / 255);
            }
            t.px[o + 3] = 255;
        }
}

// Static background layer (PC1/fill + HUD strip + tiles + cave sign).  In HD it
// is composed once at 320x200 and upscaled whole, so tile borders sit inside
// the kernel: per-tile upscales leave a seam at every tile edge (icy water
// pool).

// Static base: PC1 image / solid fill + HUD label strip.  Scale-aware.
void draw_bg_base(RenderTarget& t, systems::SystemsState& state,
                  const LevelRenderAssets& a) {
    if (a.visual_background && a.background.width == 320) {
        std::vector<std::uint8_t> bg(320u * 200u * 4u);
        // Clamped: a malformed PC1 can declare height > 200.
        indexed_to_rgba(a.background.pixels, a.background.palette, bg.data(),
                        320u * 200u);
        blit_full_rgba(t, bg, 320, 200, /*keyed=*/false);
    } else {
        Rgb fillc{0, 0, 0};
        if (a.bg_fill_index >= 0 &&
            a.bg_fill_index < static_cast<int>(a.palette.size())) {
            fillc = a.palette[static_cast<std::size_t>(a.bg_fill_index)];
        }
        const std::size_t n = static_cast<std::size_t>(t.w) * t.h * 4;
        for (std::size_t i = 0; i < n; i += 4) {
            t.px[i] = fillc.r; t.px[i + 1] = fillc.g;
            t.px[i + 2] = fillc.b; t.px[i + 3] = 255;
        }
    }
    if (!a.hud_strip.empty() &&
        (!a.visual_background || state.cave_flag || state.secret_flag)) {
        const int strip_h =
            static_cast<int>(a.hud_strip.size() / (320u * 4u));
        blit_full_rgba(t, a.hud_strip, 320, strip_h, /*keyed=*/true);
    }
}

// Enhanced: continue the backdrop up through the HUD strip (rows 0-8) by
// mirroring the 9 rows below it (row 8 = row 9 ... row 0 = row 17).  PC1 levels
// only (L1, L5: uniform sky at the top).  Tile/fill levels do it at the source
// (L7 adds a y=-54 backdrop row; L3's base fill already is the backdrop); a
// mirror there would reflect foreground.  Run after base + tiles, before any
// entity/HUD draw.
constexpr int kHudStripRows = 9;
void extend_top_backdrop(RenderTarget& t, const LevelRenderAssets& a) {
    if (!a.extend_top_backdrop) return;
    if (!a.visual_background) return;   // tile/fill levels fix it at the source
    const int band = kHudStripRows * t.scale;
    if (t.h < 2 * band || t.px == nullptr) return;
    const std::size_t stride = static_cast<std::size_t>(t.w) * 4;
    for (int y = 0; y < band; ++y)
        std::memcpy(t.px + static_cast<std::size_t>(y) * stride,
                    t.px + static_cast<std::size_t>(2 * band - 1 - y) * stride,
                    stride);
}

// Static tile placements + cave sign (scenery at the cave's right edge).
void draw_bg_tiles(RenderTarget& t, systems::SystemsState& state,
                   const LevelRenderAssets& a) {
    for (const auto& td : a.tiles) {
        if (td.sprite_idx >= 0 &&
            td.sprite_idx < static_cast<int>(a.tile_sprites.size())) {
            blit_sprite(t, a.tile_sprites[static_cast<std::size_t>(
                                td.sprite_idx)], a.palette, td.x, td.y);
        }
    }
    if (state.cave_flag && state.cave_index >= 0 &&
        state.cave_index < static_cast<int>(core::game_tables().cave_sizes.size())) {
        for (const Entity& se : state.entities) {
            if (se.obj_type == ObjType::CaveSign) {
                constexpr int kSprCaveSign = 142;
                const int exit_x = core::game_tables().cave_sizes[static_cast<std::size_t>(
                                       state.cave_index)] - 8;
                if (kSprCaveSign < static_cast<int>(a.entity_sprites.size())) {
                    blit_sprite(t, a.entity_sprites[kSprCaveSign], a.palette,
                                exit_x - 15, 115);
                }
                break;
            }
        }
    }
}

}  // namespace

// Cache key: every input that changes the static layer's pixels.  Cheap, so a
// hit never recomposes or rehashes the layer.
std::uint64_t static_bg_key(const systems::SystemsState& state,
                            const LevelRenderAssets& a, int scale,
                            const std::string& profile) {
    formats::Hash64 key;
    auto mix = [&](std::uint64_t v) { key.mix(v); };
    mix(static_cast<std::uint64_t>(state.current_level));
    mix(static_cast<std::uint64_t>(state.current_screen));
    mix(state.cave_flag ? 1u : 0u);
    mix(static_cast<std::uint64_t>(state.cave_index) + 1);
    mix(state.secret_flag ? 1u : 0u);
    mix(static_cast<std::uint64_t>(scale));
    for (const char c : profile) mix(static_cast<unsigned char>(c));
    mix(a.visual_background ? 1u : 0u);
    mix(static_cast<std::uint64_t>(a.bg_fill_index) + 1);
    for (const Rgb& c : a.palette) { mix(c.r); mix(c.g); mix(c.b); }
    for (const auto& td : a.tiles) {
        mix(static_cast<std::uint64_t>(td.sprite_idx) + 1);
        mix(static_cast<std::uint64_t>(td.x & 0xFFFF));
        mix(static_cast<std::uint64_t>(td.y & 0xFFFF));
    }
    return key.value();
}

namespace {

struct StaticBgEntry { std::uint64_t key; std::vector<std::uint8_t> hd; };
std::deque<StaticBgEntry> g_static_bg_cache;   // front = most-recently-used
constexpr std::size_t kStaticBgCacheMax = 6;

const std::vector<std::uint8_t>& get_static_bg_hd(
    systems::SystemsState& state, const LevelRenderAssets& a, int scale,
    const std::string& profile) {
    const std::uint64_t key = static_bg_key(state, a, scale, profile);
    for (auto& e : g_static_bg_cache) {
        if (e.key == key) return e.hd;
    }
    // Miss: compose at 320x200, upscale the whole layer once, cache it.
    std::vector<std::uint8_t> native(320u * 200u * 4u, 0);
    RenderTarget nt{native.data(), 320, 200, 1, nullptr, nullptr};
    nt.clip_y = 1 << 28;
    draw_bg_base(nt, state, a);
    draw_bg_tiles(nt, state, a);
    extend_top_backdrop(nt, a);
    g_static_bg_cache.push_front(StaticBgEntry{
        key, enhance::upscale_rgba(native, 320, 200, scale, profile)});
    while (g_static_bg_cache.size() > kStaticBgCacheMax)
        g_static_bg_cache.pop_back();
    return g_static_bg_cache.front().hd;
}

// Widescreen static-bg HD cache: the wide background (centre + margins) is
// static per screen, so upscale it once and per frame only copy it and draw
// sprites.  Key: static_bg_key + margin + neighbour screens + backdrop.
struct WideBgEntry { std::uint64_t key; int margin; std::vector<std::uint8_t> hd; };
std::deque<WideBgEntry> g_static_wide_bg_cache;   // front = most-recently-used
constexpr std::size_t kStaticWideBgCacheMax = 4;
}  // namespace

void redraw_bg_tiles(RenderTarget& t, systems::SystemsState& state,
                     const LevelRenderAssets& a) {
    draw_bg_tiles(t, state, a);   // anon-namespace impl, visible in this TU
}

void continue_l1_end_water(const systems::SystemsState& state,
                           const LevelRenderAssets& a, int origin_x, int buf_w,
                           std::vector<std::uint8_t>& wide) {
    // L1 end (island in a lake): continue the lake's water into the right
    // margin and the centre void past the island.  The water is sprite 7 as a
    // low band (y~185); stamp it at the same y and x-stride from the island's
    // right edge, taken from tile data so the level-end fade gets the same
    // result.  Called from compose_static_wide_bg_native and again after the
    // fade's centre overlay.  Self-gating: L1's last screen or its pseudo-exit
    // (kLastScreen+1).
    const bool l1_end = state.current_level == 1 &&
                        (state.current_screen == core::kLastScreen ||
                         state.current_screen == core::kLastScreen + 1);
    if (!l1_end) return;
    constexpr int kWaterSpr = 7;            // L1 water tile
    if (kWaterSpr >= static_cast<int>(a.tile_sprites.size())) return;
    const auto& water = a.tile_sprites[static_cast<std::size_t>(kWaterSpr)];
    std::vector<int> xs;
    int water_y = 185;
    for (const auto& tp : a.tiles)
        if (tp.sprite_idx == kWaterSpr) { xs.push_back(tp.x); water_y = tp.y; }
    if (xs.empty()) return;
    std::sort(xs.begin(), xs.end());
    int stride = water.width;               // fallback: tile width
    for (std::size_t i = 1; i < xs.size(); ++i)
        if (xs[i] - xs[i - 1] > 0) stride = std::min(stride, xs[i] - xs[i - 1]);
    // Island right edge at the water row: the rightmost non-water tile there
    // (tile data, stable across the fade).
    const int wy = std::min(199, water_y + 9);
    int max_right = 0;
    bool any = false;
    for (const auto& tp : a.tiles) {
        if (tp.sprite_idx == kWaterSpr || tp.sprite_idx < 0 ||
            tp.sprite_idx >= static_cast<int>(a.tile_sprites.size()))
            continue;
        const auto& s = a.tile_sprites[static_cast<std::size_t>(tp.sprite_idx)];
        if (tp.y <= wy && wy < tp.y + s.height) {
            max_right = std::max(max_right, tp.x + s.width);
            any = true;
        }
    }
    const int edge_x = any ? std::min(320, max_right) : 320;
    // Draw into `wide` (buf_w wide) with screen x=0 at origin_x.
    RenderTarget wt{wide.data(), buf_w, 200, 1, nullptr, nullptr};
    wt.origin_x = origin_x;
    wt.clip_x_lo = origin_x + edge_x;       // start just past the island
    wt.clip_x_hi = buf_w;
    for (int x = xs.back() + stride; origin_x + x < buf_w; x += stride)
        blit_sprite(wt, water, a.palette, x, water_y);
}

const SeamTiles::Tiles SeamTiles::kNone{};

namespace {

struct RowNeighbours {
    bool left = false;
    bool right = false;
};

// Whether the same tile sits flush against `tp` on either side, in its row.
RowNeighbours row_neighbours(
    const std::vector<LevelRenderAssets::TileDraw>& tiles,
    const LevelRenderAssets::TileDraw& tp, int w) {
    RowNeighbours nb;
    for (const auto& o : tiles) {
        if (o.sprite_idx != tp.sprite_idx || o.y != tp.y) continue;
        if (o.x == tp.x + w) nb.right = true;
        if (o.x == tp.x - w) nb.left = true;
    }
    return nb;
}

}  // namespace

// No-neighbour margin: redraw the bg tiles unclipped at origin_x = margin, so
// a wide tile authored past the screen edge (the L3 end trunk #22, x=96,
// w=288 -> 384) reaches the widescreen edge.  The x-clip protects the opposite
// margin's peek.  Skipped on the L1 end screen: the margin is open sky there.
void fill_no_neighbour_margin(std::vector<std::uint8_t>& wide, int wide_w,
                              int margin, const LevelRenderAssets& a,
                              const systems::SystemsState& state,
                              const FrameBuffer* left,
                              const FrameBuffer* right,
                              const EdgeMarginPolicy& policy) {
    if ((left != nullptr && right != nullptr) || policy.skip_tile_extension)
        return;
    RenderTarget wt{wide.data(), wide_w, 200, 1, nullptr, nullptr};
    wt.origin_x = margin;
    wt.clip_x_lo = (left != nullptr) ? margin : 0;   // protect a real peek
    wt.clip_x_hi = (right != nullptr) ? margin + 320 : wide_w;
    // L7: a single edge tile continues too (L7-18's rock wall is one column
    // wide in its bottom rows).
    const bool l7 = state.current_level == 7;
    // Build the margin from the background layers, not a mirror: each bg tile
    // in authored order (backdrop -> trunk -> ground), wide tiles spill
    // through, and horizontal rows (forest #31, dirt floor #1) repeat from
    // their edge-most member to the widescreen edge.
    for (const auto& tp : a.tiles) {
        if (tp.sprite_idx < 0 ||
            tp.sprite_idx >= static_cast<int>(a.tile_sprites.size()))
            continue;
        const auto& spr =
            a.tile_sprites[static_cast<std::size_t>(tp.sprite_idx)];
        const int w = spr.width;
        // Dead-end strip (screens 9/17): the giant trunk's grain (ELEML3 24/25,
        // a column at x~176) must not be repeated into the margin by the row
        // continuation.  The centre keeps it.
        const bool l3_trunk_tile =
            policy.void_ground_right &&
            (tp.sprite_idx == 24 || tp.sprite_idx == 25);
        blit_sprite(wt, spr, a.palette, tp.x, tp.y);   // tile itself (spills)
        const RowNeighbours nb = row_neighbours(a.tiles, tp, w);
        // Only continue rows that reach the screen edge (backdrop and floor
        // bands).  A short run (a platform, e.g. the stamped descent overlay)
        // would paint across the whole bezel.
        if (right == nullptr && !nb.right && tp.x + w >= 320 &&
            (nb.left || l7) && !l3_trunk_tile)            // rightmost, at edge
            for (int x = tp.x + w; x < 320 + margin; x += w)
                blit_sprite(wt, spr, a.palette, x, tp.y);
        if (left == nullptr && !nb.left && tp.x <= 0 &&
            (nb.right || l7))                             // leftmost, at edge
            for (int x = tp.x - w; x + w > -margin; x -= w)
                blit_sprite(wt, spr, a.palette, x, tp.y);
    }
}

// Seam overhangs: a neighbour's straddling tile may cross into the centre but
// sits under the centre's authored tiles.
void apply_seam_layering(std::vector<std::uint8_t>& wide, int wide_w,
                         int margin, const LevelRenderAssets& a,
                         const SeamTiles& seams) {
    const auto& left_seam = seams.left;
    const auto& right_seam = seams.right;
    const auto& left_bridge = seams.left_bridge;
    const auto& right_bridge = seams.right_bridge;
// The current screen's straddlers are completed inside the peeks (underlay),
// not here.  A neighbour's overhang crosses into the centre (its ~16 px
// continuation; clipping at the seam slices the bark), then the centre's level
// tiles (a.tiles[backdrop_tile_count..]) are redrawn clipped to the centre, so
// authored content wins and the overhang shows only over backdrop.
{
    RenderTarget tt{wide.data(), wide_w, 200, 1, nullptr, nullptr};
    tt.origin_x = margin;
    auto blit_tiles =
        [&](const std::vector<LevelRenderAssets::TileDraw>& tiles,
            int dx) {
            for (const auto& tp : tiles) {
                if (tp.sprite_idx < 0 ||
                    tp.sprite_idx >=
                        static_cast<int>(a.tile_sprites.size()))
                    continue;
                blit_sprite(tt,
                            a.tile_sprites[static_cast<std::size_t>(
                                tp.sprite_idx)],
                            a.palette, tp.x + dx, tp.y);
            }
        };
    // Neighbour straddlers: centre only.  Their margin part is already in the
    // peek with the neighbour's z-order.
    tt.clip_x_lo = margin;
    tt.clip_x_hi = margin + 320;
    blit_tiles(left_seam, -320);
    blit_tiles(right_seam, +320);
    // Seam bridges are synthetic (not in the peek): draw into their own margin
    // too.
    tt.clip_x_lo = -(1 << 28);
    tt.clip_x_hi = margin + 320;
    blit_tiles(left_bridge, -320);
    tt.clip_x_lo = margin;
    tt.clip_x_hi = 1 << 28;
    blit_tiles(right_bridge, +320);
    // Restore the centre's authored level tiles over the overhang.
    if (!left_seam.empty() || !right_seam.empty() ||
        !left_bridge.empty() || !right_bridge.empty()) {
        tt.clip_x_lo = margin;
        tt.clip_x_hi = margin + 320;
        const int n0 = a.backdrop_tile_count;
        for (std::size_t i = static_cast<std::size_t>(n0 < 0 ? 0 : n0);
             i < a.tiles.size(); ++i) {
            const auto& tp = a.tiles[i];
            if (tp.sprite_idx < 0 ||
                tp.sprite_idx >= static_cast<int>(a.tile_sprites.size()))
                continue;
            blit_sprite(tt,
                        a.tile_sprites[static_cast<std::size_t>(
                            tp.sprite_idx)],
                        a.palette, tp.x, tp.y);
        }
    }
}
}

void compose_static_wide_bg_native(
    systems::SystemsState& state, const LevelRenderAssets& a, int margin,
    const FrameBuffer* left, const FrameBuffer* right,
    const FrameBuffer* backdrop, std::vector<std::uint8_t>& wide,
    SeamTiles seams) {

    // Centre static layer at 320, assembled wide (margins from neighbours /
    // backdrop / self-tile), with the background layers extended into
    // no-neighbour margins.  No upscale: get_static_wide_bg_hd upscales +
    // caches this; the L3 descent uses it directly.
    FrameBuffer center{};   // 320x200
    {
        RenderTarget nt{center.px.data(), 320, 200, 1, nullptr, nullptr};
        nt.clip_y = 1 << 28;
        draw_bg_base(nt, state, a);
        draw_bg_tiles(nt, state, a);
    }
    // Per-screen margin rules (first/last screen, cave halls):
    // edge_margin_policy.hpp.
    const EdgeMarginPolicy policy =
        edge_margin_policy(state, /*has_backdrop=*/backdrop != nullptr,
                           /*screen_uses_backdrop=*/a.visual_background,
                           /*has_left=*/left != nullptr,
                           /*has_right=*/right != nullptr);
    compose_widescreen(wide, margin, center, left, right, MarginFill{/*hud_rows=*/0,
                       backdrop, /*reflect_pure=*/false,
                       /*margin_edge_brightness=*/1.0f,
                       /*repeat_no_backdrop=*/policy.repeat_no_backdrop,
                       /*ground_backdrop=*/policy.sky_only,
                       /*void_ground_left=*/false,
                       /*void_ground_right=*/policy.void_ground_right});
    const int wide_w = 320 + 2 * margin;
    // Tile levels (3/7, no FOND): a no-neighbour margin starts black, then the
    // layer extension continues only authored patterns (backdrop rows, floors,
    // walls).  A self-tile mirror would clone edge objects (S13's half door).
    // FOND levels keep the backdrop extension; secret rooms keep the self-tile.
    const bool tile_level_edge = policy.black_base;
    // L7 cave hall (screens 10-12): the outer seams (warps to S9 / S13) stay
    // pure black, like cave interiors; no row continuation there.

    if (tile_level_edge) {
        auto fill_black = [&](int x0, int x1) {
            for (int y = 0; y < 200; ++y)
                for (int x = x0; x < x1; ++x) {
                    std::uint8_t* p =
                        &wide[(static_cast<std::size_t>(y) * wide_w + x) * 4];
                    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
                }
        };
        if (left == nullptr) fill_black(0, margin);
        if (right == nullptr) fill_black(margin + 320, wide_w);
    }
    fill_no_neighbour_margin(wide, wide_w, margin, a, state, left, right,
                             policy);
    // L1 end: continue the lake water (runs again after the fade's centre
    // overlay; see continue_l1_end_water).
    if (policy.water_continues)
        continue_l1_end_water(state, a, /*origin_x=*/margin,
                              /*buf_w=*/320 + 2 * margin, wide);
    // Seam straddlers (trunks, pillars, bushes like S16's foliage at x=256) are
    // cut by the peek.  Rebuild them whole: the current screen's overhang into
    // the adjacent margin, the neighbours' straddlers re-blitted at -/+320.
    apply_seam_layering(wide, wide_w, margin, a, seams);
    // Extend the backdrop through the HUD strip across the full wide width (no
    // black corners).  Last, so its source rows are final.
    RenderTarget wide_t{wide.data(), 320 + 2 * margin, 200, 1, nullptr, nullptr};
    extend_top_backdrop(wide_t, a);
}

const std::vector<std::uint8_t>& get_static_wide_bg_hd(
    systems::SystemsState& state, const LevelRenderAssets& a, int scale,
    const std::string& profile, int margin, const WidePeek& peek,
    std::uint64_t* key_out) {
    static const std::vector<LevelRenderAssets::TileDraw> kNoTiles;
    const FrameBuffer* left = peek.left;
    const int left_screen = peek.left_screen;
    const FrameBuffer* right = peek.right;
    const int right_screen = peek.right_screen;
    const FrameBuffer* backdrop = peek.backdrop;
    const std::vector<LevelRenderAssets::TileDraw>& left_seam =
        peek.left_seam != nullptr ? *peek.left_seam : kNoTiles;
    const std::vector<LevelRenderAssets::TileDraw>& right_seam =
        peek.right_seam != nullptr ? *peek.right_seam : kNoTiles;
    const std::vector<LevelRenderAssets::TileDraw>& left_bridge =
        peek.left_bridge != nullptr ? *peek.left_bridge : kNoTiles;
    const std::vector<LevelRenderAssets::TileDraw>& right_bridge =
        peek.right_bridge != nullptr ? *peek.right_bridge : kNoTiles;
    const std::uint64_t peek_generation = peek.generation;
    std::uint64_t key = static_bg_key(state, a, scale, profile);
    auto mix = [&](std::uint64_t v) { key ^= v; key *= 1099511628211ull; };
    mix(0x57494445ull);   // "WIDE" salt — never collide with the 320 cache
    // The peek buffers bake in neighbour entity state (a destroyed L7 spike
    // rock, food); the caller bumps the generation on every rebuild.
    mix(peek_generation);
    mix(static_cast<std::uint64_t>(margin));
    mix(static_cast<std::uint64_t>(left ? (left_screen + 1) : 0));
    mix(static_cast<std::uint64_t>(right ? (right_screen + 1) : 0));
    mix(backdrop ? 1u : 0u);
    // Seam tiles change the pixels, so they are part of the key.
    for (const auto* seam : {&left_seam, &right_seam, &left_bridge,
                             &right_bridge}) {
        mix(static_cast<std::uint64_t>(seam->size()));
        for (const auto& t : *seam) {
            mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.sprite_idx)));
            mix((static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.x)) << 32) |
                static_cast<std::uint32_t>(t.y));
        }
    }
    if (key_out != nullptr) *key_out = key;   // margin is mixed in above
    for (auto& e : g_static_wide_bg_cache) {
        if (e.key == key && e.margin == margin) return e.hd;
    }
    // Miss: compose at native 320, upscale the whole wide layer once, cache it.
    const int wide_w = 320 + 2 * margin;
    std::vector<std::uint8_t> wide;
    compose_static_wide_bg_native(state, a, margin, left, right, backdrop, wide,
                                  SeamTiles{left_seam, right_seam, left_bridge,
                                            right_bridge});
    g_static_wide_bg_cache.push_front(WideBgEntry{
        key, margin, enhance::upscale_rgba(wide, wide_w, 200, scale, profile)});
    while (g_static_wide_bg_cache.size() > kStaticWideBgCacheMax)
        g_static_wide_bg_cache.pop_back();
    return g_static_wide_bg_cache.front().hd;
}

void draw_background(RenderTarget& t, systems::SystemsState& state,
                     const LevelRenderAssets& a,
                     const std::function<void(RenderTarget&)>& post_background_hook) {
    t.clip_y = 1 << 28;   // no clip for background + floor tiles (reset stale)

    // 1+2. Static background layer.  HD: from the per-screen cache (seamless
    // whole upscale).  With a post-background hook (secret bubbles between base
    // and floor) or at scale 1: direct path.
    const bool cache_static_bg =
        t.hd_path() &&
        post_background_hook == nullptr &&
        t.w == 320 * t.scale && t.h == 200 * t.scale;
    if (cache_static_bg) {
        const std::vector<std::uint8_t>& hd =
            get_static_bg_hd(state, a, t.scale, *t.profile);
        const std::size_t n =
            std::min(hd.size(), static_cast<std::size_t>(t.w) * t.h * 4);
        std::memcpy(t.px, hd.data(), n);
    } else {
        draw_bg_base(t, state, a);
        // After base, before tiles (secret-room bubbles behind the floor).
        if (post_background_hook) post_background_hook(t);
        draw_bg_tiles(t, state, a);
        extend_top_backdrop(t, a);
    }

    // Secret screen: clip the foreground at y=169.  The EXE sets DS:0x84
    // (screen_height) to 168 there and its EGA/CGA/Tandy paths clamp at +1; the
    // VGA path omits it, leaving the springs over the floor (FUN_1052_2813).
    if (state.secret_flag) t.clip_y = (168 + 1) * t.scale;
}

}  // namespace olduvai::presentation
