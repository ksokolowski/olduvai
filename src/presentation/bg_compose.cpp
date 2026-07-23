// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Static (entity-free) background layer: base fill + backdrop extend + floor
// tiles + the secret-screen clip, plus the per-screen and widescreen HD
// background caches (draw_background / redraw_bg_tiles / compose_static_wide_bg_native
// / get_static_wide_bg_hd, and their anon-namespace helpers).  Moved verbatim
// out of game_render.cpp so that file is the foreground/entity + compose glue
// (SOC roadmap: bg_compose).  Public entry points are declared in
// game_render.hpp; the caches and tile helpers stay file-local here.
#include "presentation/game_render.hpp"

#include "presentation/tile_patterns.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>

#include "core/game_tables.hpp"
#include "enhance/upscale.hpp"
#include "presentation/widescreen.hpp"
#include "systems/cave_logic.hpp"

namespace olduvai::presentation {

using core::Entity;
using core::MonsterState;
using core::ObjType;
using formats::Rgb;
using formats::Sprite;

namespace {
// Blit a full-frame 320×200 RGBA source into the target.  scale 1 = direct
// pixel write (byte-identical to the legacy inline loops); scale>1 routes the
// whole image through the asset cache (per-asset background upscale) then
// copies.  `keyed`: skip alpha-0 source pixels (the colorkeyed HUD strip);
// otherwise treat the source as opaque (the background image).
void blit_full_rgba(RenderTarget& t, const std::vector<std::uint8_t>& src,
                    int sw, int sh, bool keyed) {
    if (t.scale <= 1) {
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

// ── Static background layer (PC1/fill + HUD strip + tiles + cave sign) ──
// Factored out of compose_frame so the HD path can compose it ONCE at native
// 320×200 and upscale the WHOLE layer (tile borders become interior to the
// omniscale kernel → no per-tile seams, e.g. the icy-land water pool).  The
// per-tile path upscaled each tile in isolation, edge-clamping at its borders
// and leaving a light+dark seam at every boundary.

// Static base: PC1 image / solid fill + HUD label strip.  Scale-aware.
void draw_bg_base(RenderTarget& t, systems::SystemsState& state,
                  const LevelRenderAssets& a) {
    if (a.visual_background && a.background.width == 320) {
        std::vector<std::uint8_t> bg(320u * 200u * 4u);
        // Clamp to the destination: a malformed PC1 can declare height > 200
        // (pixels.size() = w*h from the file's BMHD) — without the cap the
        // loop writes past the fixed 320x200 buffer.
        const std::size_t n =
            std::min(a.background.pixels.size(), std::size_t{320u * 200u});
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t idx = a.background.pixels[i];
            const Rgb c = (idx < a.background.palette.size())
                              ? a.background.palette[idx] : Rgb{};
            bg[i * 4] = c.r; bg[i * 4 + 1] = c.g;
            bg[i * 4 + 2] = c.b; bg[i * 4 + 3] = 255;
        }
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

// Enhanced-only: continue the level backdrop up through the top HUD-strip band
// (native rows 0..8) so the Score/Lives/Time line floats over the backdrop
// instead of the bare base-fill "black bar".
//
// PC1-IMAGE LEVELS ONLY (L1 jungle, icy): the FOND backdrop is a full-screen
// image whose top is uniform sky, so a mirror of the 9-row slice just below the
// band (row[8]=row[9] … row[0]=row[17]) reads seamlessly.  TILE / FILL levels
// (L7 lavarock, L3 dark woods) are handled at the SOURCE instead — L7 extends
// its real backdrop tiling up (bind_screen adds a y=-54 row); L3's dark base
// fill already IS the backdrop above the canopy — because mirroring their
// composed frame would reflect FOREGROUND (trunk grain, foliage, platforms)
// into the band (doubled trunk caps / floating bush bits).  Hence the
// visual_background gate: only PC1 levels take the mirror.
//
// MUST run after draw_bg_base + draw_bg_tiles and BEFORE any entity/HUD draw so
// the source is pure backdrop.  Scale-aware; no-op unless a.extend_top_backdrop.
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

// Explicit cache key — every input that changes the static layer's pixels.
// Cheap to compute (a few hundred bytes) so we never recompose/rehash the
// full 320×200 layer on a cache hit; (level, screen, palette, tiles) is stable
// across many frames during normal play.
std::uint64_t static_bg_key(const systems::SystemsState& state,
                            const LevelRenderAssets& a, int scale,
                            const std::string& profile) {
    std::uint64_t k = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) { k ^= v; k *= 1099511628211ull; };
    mix(static_cast<std::uint64_t>(state.current_level));
    mix(static_cast<std::uint64_t>(state.current_screen));
    mix(state.cave_flag ? 1u : 0u);
    mix(static_cast<std::uint64_t>(state.cave_index + 1));
    mix(state.secret_flag ? 1u : 0u);
    mix(static_cast<std::uint64_t>(scale));
    for (char c : profile) mix(static_cast<unsigned char>(c));
    mix(a.visual_background ? 1u : 0u);
    mix(static_cast<std::uint64_t>(a.bg_fill_index + 1));
    for (const Rgb& c : a.palette) { mix(c.r); mix(c.g); mix(c.b); }
    for (const auto& td : a.tiles) {
        mix(static_cast<std::uint64_t>(td.sprite_idx + 1));
        mix(static_cast<std::uint64_t>(td.x & 0xFFFF));
        mix(static_cast<std::uint64_t>(td.y & 0xFFFF));
    }
    return k;
}

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
    // Miss: compose the static layer at NATIVE 320×200, then upscale the WHOLE
    // layer once (seamless), and cache the HD pixels keyed by the screen.
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

// ── Widescreen static-bg HD cache (twin of get_static_bg_hd) ────────────────
// The WIDE static background (centre bg+tiles + the peek margins) is, like the
// 320 centre, fully static within a flip-screen screen — only sprites/HUD move.
// Caching the upscaled wide layer per screen lets the widescreen present skip
// the per-frame whole-frame upscale (the omniscale-too-slow finding, task #61):
// compose once → upscale once (omniscale included, masked by the screen-change)
// → memcpy + HD per-asset sprites each frame, exactly like the non-widescreen
// path.  Keyed by static_bg_key PLUS margin + neighbour screen ids + backdrop.
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
    // L1 end (mid-air island in a lake): CONTINUE the lake's water into the
    // right no-neighbour margin (and the centre's bottom-right VOID just past the
    // island) so it reads as one body of water — no raised "wall", no gap.  The
    // screen's real water is the genuine water SPRITE (sprite 7) placed as a LOW
    // band at the bottom (y≈185, only its top ~12 px on-screen) with empty/black
    // above; repeat exactly that pattern to the right of the island.  Re-use the
    // REAL tile placements: read the screen's water tiles, then keep stamping the
    // same sprite at the SAME y and SAME x-stride past the screen edge, starting
    // at the island's right edge (from tile data, so it is stable in the
    // level-end fade where the rendered base differs).  Black above the band is
    // correct — the centre's water has the same empty space above it.
    //
    // Runs both from compose_static_wide_bg_native AND, for the level-end fade,
    // AGAIN after wrap_wide_static overlays the 320 centre (that overlay would
    // otherwise clobber the void part, which lives inside the centre region).
    // Cosmetic widescreen-only fill — gameplay uses the unmodified 320 logic.
    //
    // Self-gating so callers (compose + the post-overlay fade pass) need no
    // pre-check: only L1's last screen / its level-complete pseudo-exit, where
    // sprite 7 is the water tile.  (kLastScreen+1 = the pseudo-exit the
    // level-complete handler bumps current_screen to before the fade composes.)
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
    // Island right edge at the water row = rightmost non-water tile overlapping
    // it (tile data, not the rendered centre — stable across the fade).
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
    // Draw into `wide` (buf_w wide), placing the screen's x=0 at origin_x.  For
    // the steady/fade margin buffer origin_x = margin, buf_w = 320+2*margin; for
    // the screen-transition PANORAMA strip origin_x = the screen's slot base and
    // buf_w = the strip width — same continuation, different host buffer.
    RenderTarget wt{wide.data(), buf_w, 200, 1, nullptr, nullptr};
    wt.origin_x = origin_x;
    wt.clip_x_lo = origin_x + edge_x;       // start just past the island
    wt.clip_x_hi = buf_w;
    for (int x = xs.back() + stride; origin_x + x < buf_w; x += stride)
        blit_sprite(wt, water, a.palette, x, water_y);
}

void compose_static_wide_bg_native(
    systems::SystemsState& state, const LevelRenderAssets& a, int margin,
    const FrameBuffer* left, const FrameBuffer* right,
    const FrameBuffer* backdrop, std::vector<std::uint8_t>& wide,
    const std::vector<LevelRenderAssets::TileDraw>& left_seam,
    const std::vector<LevelRenderAssets::TileDraw>& right_seam,
    const std::vector<LevelRenderAssets::TileDraw>& left_bridge,
    const std::vector<LevelRenderAssets::TileDraw>& right_bridge) {
    // Compose the centre static layer at native 320, assemble the wide native
    // buffer (margins from neighbours / backdrop / self-tile), and extend the
    // background LAYERS into any no-neighbour margin so the backdrop, a spilling
    // wide tile (the L3 trunk #22), and the floor all reach the widescreen edge.
    // NO upscale — the HD path (get_static_wide_bg_hd) upscales + caches this;
    // the L3 trunk-descent reuses it raw to fill the descent margins.
    FrameBuffer center{};   // 320×200
    {
        RenderTarget nt{center.px.data(), 320, 200, 1, nullptr, nullptr};
        nt.clip_y = 1 << 28;
        draw_bg_base(nt, state, a);
        draw_bg_tiles(nt, state, a);
    }
    // L1 (jungle) mid-air-island END screen: the area beside the island reads as
    // OPEN SKY — fill the no-neighbour margin from the FOND backdrop only
    // (sky+mountains), NOT the extended foreground ground.  Only this screen (the
    // first/other screens keep the ground extension that looks right there).
    // current_screen is kLastScreen during play, but the level-complete handler
    // bumps it to kLastScreen+1 (the pseudo-exit) the instant level_complete
    // fires (transitions.cpp) — and the level-end fade composes from THAT state.
    // Accept both so the water margin survives into the fade's first frame
    // instead of reverting to the mirror fallback.
    const bool l1_end = state.current_level == 1 &&
                        (state.current_screen == core::kLastScreen ||
                         state.current_screen == core::kLastScreen + 1);
    // L3 trunk-entry screen 9 RIGHT edge is an IMPASSABLE dead-end (player clamped
    // at x=270; the trunk is entered going DOWN, never by walking right).  Its
    // full-width floor reaches the edge, so the no-neighbour margin mirrors a
    // walkable dirt ledge to nowhere — void just that dirt band so the strip reads
    // as "just backdrop" (forest + grass), like screen 0's left whose floor
    // doesn't reach the edge.  right==nullptr confirms the no-neighbour side.
    const bool l3_deadend_right =
        state.current_level == 3 && right == nullptr &&
        (state.current_screen == 9 || state.current_screen == 17);
    compose_widescreen(wide, margin, center, left, right, /*hud_rows=*/0,
                       backdrop, /*reflect_pure=*/false,
                       /*margin_edge_brightness=*/1.0f,
                       /*repeat_no_backdrop=*/state.secret_flag == 0,
                       /*ground_backdrop=*/l1_end && backdrop != nullptr,
                       /*void_ground_left=*/false,
                       /*void_ground_right=*/l3_deadend_right);
    const int wide_w = 320 + 2 * margin;
    // Tile-based surface levels (internal 3/7 — no FOND backdrop): a
    // no-neighbour margin gets a BLACK BASE before the tile layer-extension
    // below, replacing compose_widescreen's self-tile MIRROR of the screen's
    // edge columns.  The mirror cloned edge OBJECTS into the margin — a
    // half-door authored at S13's x=68 reflected as a phantom door sliver,
    // mostly-black cave-hall edges smeared — while the layer extension
    // rebuilds the margin from the authored PATTERNS only: backdrop rows,
    // floor bands, wall columns continue; everything unauthored stays dark,
    // consistent with the level's theme.  (The L7 cave-hall screens 10-12
    // are the same case — their outer seams are the S9/S13 warp boundaries.)
    // FOND levels (1/5) keep the validated backdrop-extension; secret rooms
    // keep their deliberate self-tile.
    const bool tile_level_edge = !a.visual_background &&
                                 state.secret_flag == 0 &&
                                 state.current_screen < 100;
    // L7 lava cave-hall (screens 10-12): the outer seams (S10 left = the S9
    // cave-descent warp, S12 right = the S13 teleport warp) stay PURE BLACK —
    // like the regular cave interiors (user-picked after seeing the darkness-
    // rows and gray-wall alternatives): the warp boundaries are impassable
    // and the hall reads as a closed cave.  The l7_cave_hall flag below also
    // skips the row-continuation for these margins.
    const bool l7_cave_hall = state.current_level == 7 &&
                              state.current_screen >= 10 &&
                              state.current_screen <= 12;
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
    // No-neighbour margin: re-draw the static bg TILES into the wide buffer
    // UN-CLIPPED (origin_x = margin) so a wide bg tile authored PAST the screen
    // edge — the L3 level-end tree trunk #22 (x=96, width 288 → x=384) — draws on
    // through into the margin instead of being clipped at 320 and mirror-mangled.
    // x-clip protects the OPPOSITE margin where a real neighbour peek lives.
    // L1 end screen: compose_widescreen already filled the margin with pure
    // backdrop (sky+mountains); the tile layer-extension would draw the island's
    // ground/palm back into it, so skip it there (open sky beside the island).
    if ((left == nullptr || right == nullptr) && !l1_end && !l7_cave_hall) {
        RenderTarget wt{wide.data(), wide_w, 200, 1, nullptr, nullptr};
        wt.origin_x = margin;
        wt.clip_x_lo = (left != nullptr) ? margin : 0;   // protect a real peek
        wt.clip_x_hi = (right != nullptr) ? margin + 320 : wide_w;
        // Compose the no-neighbour margin from the real background LAYERS
        // extended to the screen edge (NOT a mirror, which duplicated the trunk):
        // draw each bg tile in its authored order so the layering is preserved
        // (backdrop → trunk → ground), letting a wide tile (the L3 trunk #22,
        // x=96 w=288 → x=384) spill through; AND continue any horizontal tile ROW
        // (the forest backdrop #31, the dirt-floor #1) into the margin by
        // repeating it from the edge-most member, so the backdrop and floor reach
        // the widescreen edge just like the centre.
        for (const auto& tp : a.tiles) {
            if (tp.sprite_idx < 0 ||
                tp.sprite_idx >= static_cast<int>(a.tile_sprites.size()))
                continue;
            const auto& spr =
                a.tile_sprites[static_cast<std::size_t>(tp.sprite_idx)];
            const int w = spr.width;
            // Dead-end strip (screen 9/17): the level-end giant trunk wood-grain
            // (ELEML3 tiles 24/25, a vertical COLUMN at x≈176) ends exactly at the
            // screen edge, so its own blit doesn't spill — but the row
            // continuation below would REPEAT it into the no-neighbour margin.
            // Suppress that so the strip stays pure forest backdrop (#31 + torus)
            // + voided floor; the CENTRE copy keeps the trunk.  (Screen 9 has no
            // 24/25 tiles, so this is a no-op there.)
            const bool l3_trunk_tile =
                l3_deadend_right && (tp.sprite_idx == 24 || tp.sprite_idx == 25);
            blit_sprite(wt, spr, a.palette, tp.x, tp.y);   // tile itself (spills)
            bool right_nb = false, left_nb = false;        // row membership
            for (const auto& o : a.tiles)
                if (o.sprite_idx == tp.sprite_idx && o.y == tp.y) {
                    if (o.x == tp.x + w) right_nb = true;
                    if (o.x == tp.x - w) left_nb = true;
                }
            // ONLY continue a row that already spans to the SCREEN EDGE — a
            // full-width background band (forest backdrop #31, dirt floor #1).
            // A short foreground run (a platform — in particular the descent-
            // overlay platform tiles the trunk-descent STAMPS into the tile list
            // after Phase 2) does NOT reach the edge, so repeating it would paint
            // an extra platform across the whole bezel.  Gate on edge-reach.
            //
            // L7 (volcanic): drop the left_nb requirement so a SINGLE edge tile
            // continues too — L7-18's right rock wall (sprite 4) is a 2-column
            // block up top but a SINGLE column at the bottom rows (y=159/189);
            // without this the bottom rock isn't repeated and the lava backdrop
            // shows through (the "cut" strip).  The wide L3 trunk #22 is unharmed
            // (it spills via its own blit, on a non-L7 level).
            const bool l7 = state.current_level == 7;
            if (right == nullptr && !right_nb && tp.x + w >= 320 &&
                (left_nb || l7) && !l3_trunk_tile)            // rightmost, at edge
                for (int x = tp.x + w; x < 320 + margin; x += w)
                    blit_sprite(wt, spr, a.palette, x, tp.y);
            if (left == nullptr && !left_nb && tp.x <= 0 &&
                (right_nb || l7))                             // leftmost, at edge
                for (int x = tp.x - w; x + w > -margin; x -= w)
                    blit_sprite(wt, spr, a.palette, x, tp.y);
        }
    }
    // L1 end (mid-air island in a lake): continue the lake's water into the
    // right margin (+ the centre's bottom-right void past the island).  Must run
    // AGAIN after wrap_wide_static's centre overlay (which would clobber the
    // void part), so it lives in a shared helper — see continue_l1_end_water.
    if (l1_end && right == nullptr)
        continue_l1_end_water(state, a, /*origin_x=*/margin,
                              /*buf_w=*/320 + 2 * margin, wide);
    // Seam-straddle continuity (tile_patterns): any tile that straddles a
    // screen seam — trunk/pillar columns AND lone bushes (Dark Woods S16's
    // spr-5 foliage at x=256, w=80) — is cut by the peek: the current screen
    // clips at its own 320/0 edge and the neighbour's copy sits at a
    // different x, so the halves never align.  Rebuild every straddler whole
    // from its own tiles:
    //   * the CURRENT screen's straddling tiles, re-drawn so their overhang
    //     spills into the adjacent margin (both edges);
    //   * the NEIGHBOURS' straddling tiles (collected by the caller via
    //     tile_patterns::seam_straddling_tiles), re-blitted at ∓320 so the
    //     peeked trunk/bush is completed within the margin.
    // LAYERING RULES (both user-verified the hard way):
    //   * The CURRENT screen's straddle redraw stays MARGINS-ONLY — its centre
    //     part already exists with the authored z-order; re-blitting it in the
    //     centre put a column over tiles the authored order draws above it.
    //   * A NEIGHBOUR's seam overhang may cross the seam into the centre (the
    //     trunk's genuine ~16px continuation — clipping it at the seam line
    //     left a sliced bark edge, Dark Woods S14), but it must sit UNDER the
    //     centre's authored level tiles: after the seam blits, the centre's
    //     level tiles (a.tiles[backdrop_tile_count..]) are redrawn clipped to
    //     the centre, so authored content always wins (L7 S13's rock wall)
    //     while the overhang stays visible where the centre has only backdrop.
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
        // (The CURRENT screen's straddling tiles are completed INSIDE the
        // neighbour peeks as underlay — compose_surface_screen_static's
        // `underlay` param — so their margin part sits UNDER the neighbour's
        // authored tiles; no margins-only re-blit here.)
        // Neighbour seam straddlers → CENTRE-ONLY overhang.  Their margin
        // part already exists in the peek WITH the neighbour's authored
        // z-order (dirt-top rows draw over subsurface rock there); the
        // earlier own-margin re-blit painted the lone straddler back OVER
        // the finished peek and buried S14's dirt layer under its own
        // (4,-16,141) subsurface rock.
        tt.clip_x_lo = margin;
        tt.clip_x_hi = margin + 320;
        blit_tiles(left_seam, -320);
        blit_tiles(right_seam, +320);
        // Seam-hole BRIDGES are synthetic (tile_patterns::seam_row_bridges)
        // — the peek does NOT contain them, so they draw into their own
        // margin too (protect only the opposite margin).
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
    // Extend the backdrop up through the top HUD-strip band across the FULL
    // wide width — center AND both margins — so the Score/Lives/Time line floats
    // over a continuous backdrop with no black notch in the corner strips.  Runs
    // last (after the margins are assembled) so its 9..17 source rows are the
    // final composed backdrop everywhere.
    RenderTarget wide_t{wide.data(), 320 + 2 * margin, 200, 1, nullptr, nullptr};
    extend_top_backdrop(wide_t, a);
}

const std::vector<std::uint8_t>& get_static_wide_bg_hd(
    systems::SystemsState& state, const LevelRenderAssets& a, int scale,
    const std::string& profile, int margin,
    const FrameBuffer* left, int left_screen,
    const FrameBuffer* right, int right_screen,
    const FrameBuffer* backdrop,
    const std::vector<LevelRenderAssets::TileDraw>& left_seam,
    const std::vector<LevelRenderAssets::TileDraw>& right_seam,
    const std::vector<LevelRenderAssets::TileDraw>& left_bridge,
    const std::vector<LevelRenderAssets::TileDraw>& right_bridge,
    std::uint64_t peek_generation) {
    std::uint64_t key = static_bg_key(state, a, scale, profile);
    auto mix = [&](std::uint64_t v) { key ^= v; key *= 1099511628211ull; };
    mix(0x57494445ull);   // "WIDE" salt — never collide with the 320 cache
    // The peek buffers' CONTENT is invisible to this key, but a neighbour's
    // live entity state is baked into them (static-class objects: a destroyed
    // L7 spike rock, food) — the caller bumps the generation whenever it
    // rebuilds the peeks, so a revisit never reuses a stale margin.
    mix(peek_generation);
    mix(static_cast<std::uint64_t>(margin));
    mix(static_cast<std::uint64_t>(left ? (left_screen + 1) : 0));
    mix(static_cast<std::uint64_t>(right ? (right_screen + 1) : 0));
    mix(backdrop ? 1u : 0u);
    // Seam-completion tiles change the composed pixels, so they must be part
    // of the key: a caller passing empty seams must never be served a cached
    // seam-bearing frame for the same screen/margin (or vice versa).
    for (const auto* seam : {&left_seam, &right_seam, &left_bridge,
                             &right_bridge}) {
        mix(static_cast<std::uint64_t>(seam->size()));
        for (const auto& t : *seam) {
            mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.sprite_idx)));
            mix((static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.x)) << 32) |
                static_cast<std::uint32_t>(t.y));
        }
    }
    for (auto& e : g_static_wide_bg_cache) {
        if (e.key == key && e.margin == margin) return e.hd;
    }
    // Miss: compose the centre static layer + wide-margin assembly at native
    // 320 (shared helper), then upscale the WHOLE wide layer once and cache it.
    const int wide_w = 320 + 2 * margin;
    std::vector<std::uint8_t> wide;
    compose_static_wide_bg_native(state, a, margin, left, right, backdrop, wide,
                                  left_seam, right_seam, left_bridge,
                                  right_bridge);
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

    // 1+2. Static background layer (PC1/fill + HUD strip + tiles + cave sign).
    // In HD, compose it once at native and upscale the WHOLE layer (cached by
    // screen key) so tile borders are interior to the omniscale kernel — no
    // per-tile seams (icy-land water pool, etc.).  The post-background hook
    // (enhanced secret bubbles) must interleave BETWEEN the base and the floor
    // tiles, so when a hook is present we fall back to the direct per-asset
    // path; classic (scale 1) always uses the direct path (no upscale, no
    // seam, byte-identical).
    const bool cache_static_bg =
        t.scale > 1 && t.cache != nullptr && t.profile != nullptr &&
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
        // Post-background hook — after base, BEFORE tiles (enhanced-mode secret
        // room draws fluid bubbles behind the floor tiles).
        if (post_background_hook) post_background_hook(t);
        draw_bg_tiles(t, state, a);
        extend_top_backdrop(t, a);
    }

    // Secret screen: clip all FOREGROUND (entities, trampoline, player) at the
    // floor line y=169 — EXE flips DS:0x84 (screen_height) to 168 for the
    // cave/secret screens and the EGA/CGA/Tandy draw paths clamp per-row at
    // (screen_height+1); the VGA path omitted it, leaving the trampoline
    // springs poking over the floor.  Floor tiles above were drawn unclipped.
    // FUN_1052_2813; Finding exe_vga_path_screen_height_clip_omission.md.
    if (state.secret_flag) t.clip_y = (168 + 1) * t.scale;
}

}  // namespace olduvai::presentation
