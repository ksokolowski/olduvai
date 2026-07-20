// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Game frame composition — native 320×200 indexed→RGBA framebuffer.
// Mirrors the per-screen pipeline: background → background tiles → tile
// placements → entities → hazards/popups → player (+ weapon overlay).

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "enhance/hd_asset_cache.hpp"
#include "formats/mat.hpp"
#include "formats/pc1.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

struct LevelRenderAssets {
    formats::Pc1Image background;             // visual bg (L1/L5)
    bool visual_background = false;
    std::vector<formats::Rgb> palette;        // active level palette
    std::vector<formats::Sprite> tile_sprites;    // combined tile MATs
    std::vector<formats::Sprite> entity_sprites;  // LxSPR.MAT
    struct TileDraw { int sprite_idx, x, y; };
    std::vector<TileDraw> tiles;              // current screen placements
    // Top-9-row SCORE/FOOD/LIVES/TIME label strip (RGBA, colorkeyed to
    // alpha 0) for levels without a label-baking background + caves.
    std::vector<std::uint8_t> hud_strip;      // 320*9*4 or empty
    int bg_fill_index = -1;   // palette fill when no visual bg (-1 = black)
    // Enhanced-mode signal (set from use_hd_text): when true, the pre-baked
    // sprite "banners" (GET READY 132/133 in the HUD layer; the NOT-ENOUGH-FOOD
    // gate cue 82/91 in draw_entities) are SUPPRESSED here and re-drawn as
    // cartoony vector text in the output-resolution overlay instead, so they
    // match the rest of the enhanced HUD/tally typography.  Default false →
    // classic + non-vector-HD paths keep the pre-baked sprites byte-identical.
    bool enhanced_vector_banners = false;
    // Enhanced-mode signal (set from use_hd_text): continue the level backdrop
    // up through the top HUD-strip band (native rows 0..8) so the Score/Lives/
    // Time line floats over the backdrop instead of the bare base-fill "black
    // bar".  Default false → classic + faithful paths keep the EXE black strip.
    bool extend_top_backdrop = false;
    // Number of leading `tiles` entries that are bind-injected BACKDROP rows
    // (L3 pine/trunk-interior, L7 lavarock/cave strips) rather than level-data
    // tiles.  The widescreen seam pass redraws tiles[backdrop_tile_count..]
    // over a neighbour's seam overhang so the centre's authored content and
    // z-order always win, while the overhang stays visible where the centre
    // has only backdrop.
    int backdrop_tile_count = 0;
};

// RGBA frame buffer.  Default 320×200 (classic path, all non-gameplay callers).
// FrameBuffer(w,h) allocates an HD-sized buffer; w/h are carried so that
// blit_shifted and upload_and_show can be dimension-aware.
struct FrameBuffer {
    int w = 320;
    int h = 200;
    std::vector<std::uint8_t> px =
        std::vector<std::uint8_t>(320 * 200 * 4, 0);
    FrameBuffer() = default;
    FrameBuffer(int w_, int h_) : w(w_), h(h_),
        px(static_cast<std::size_t>(w_) * h_ * 4, 0) {}
};

// A scale-parametric blit/compose target.  scale 1 → a plain 320×200
// indexed-blit surface (classic, byte-identical to the old path).  scale>1
// → an HD buffer (w=320*scale, h=200*scale) composed from cached per-asset
// upscales (cache + profile must be non-null).
struct RenderTarget {
    std::uint8_t* px = nullptr;   // w*h*4 RGBA, caller-owned
    int w = 320;
    int h = 200;
    int scale = 1;
    enhance::HdAssetCache* cache = nullptr;   // null at scale 1
    const std::string* profile = nullptr;     // null at scale 1
    // Bottom clip in SCALED dst-y (exclusive): blits skip rows >= clip_y.
    // Default = no clip.  Set to (screen_height+1)*scale on the secret screen
    // so foreground sprites (trampoline springs, entities, player) are cut at
    // the floor line — the EXE EGA-path clamp the VGA path omitted
    // (FUN_1052_2813; Finding exe_vga_path_screen_height_clip_omission.md).
    int clip_y = 1 << 28;
    // Horizontal clip window in SCALED dst-x [clip_x_lo, clip_x_hi): blits skip
    // columns outside it.  Default = no clip.  Used by the widescreen no-
    // neighbour margin pass to re-draw the background tiles UN-CLIPPED into the
    // margin (so a wide bg tile authored past the screen edge — e.g. the L3
    // level-end tree trunk #22 at x=96 width 288 → x=384 — draws on through
    // instead of being clipped at 320 and mirrored) while still protecting the
    // OPPOSITE margin where a real neighbour peek lives.
    int clip_x_lo = -(1 << 28);
    int clip_x_hi = 1 << 28;
    // Separate clip applied to the PLAYER blits only (draw_entities switches to
    // these right before drawing the player).  Lets the widescreen overflow pass
    // clip the PLAYER at a no-neighbour edge (so it can't spill onto the
    // synthetic edge-fill — the "mirrored player") while letting flying ENTITIES
    // (e.g. the bird) overflow naturally into the margin.  Default = no clip.
    int player_clip_x_lo = -(1 << 28);
    int player_clip_x_hi = 1 << 28;
    // Horizontal NATIVE-space dst-x bias added to every sprite blit (scaled by
    // `scale` internally).  Default 0 → every existing call is byte-identical.
    // The enhanced widescreen overflow pass sets origin_x = margin so live
    // entities crossing the 320 edge spill into the neighbour-terrain margins
    // (a deliberate, enhanced-only divergence from the DOS hard clip at 320).
    int origin_x = 0;
    // When false, the draw-time player/weapon STATE MUTATIONS (the club_flag
    // swing decrement and the death/cave-warp club_flag clear) are SUPPRESSED:
    // draw_entities still DRAWS the same sprites but does not mutate gameplay
    // state.  The widescreen entity-overflow pass sets this false so its draw
    // (the one that spills into the margins) is purely visual — the single
    // authoritative advance fires exactly once per gameplay frame on the main
    // fb compose (advance_state stays true there), which is captured BEFORE the
    // smooth-motion sub-frame save/restore so it survives.  Default true → every
    // existing single-compose call advances exactly once.
    bool advance_state = true;
    // When true (HD smooth-motion sub-frame), draw_entities reads Entity::fx/fy
    // and draw_player reads player_fx/player_fy (below) — the float render
    // position the smooth-motion lerp wrote — and rounds it at HD: 1-HD-pixel
    // motion granularity instead of the integer position's 4-HD-pixel snap.
    // Default false → every other path reads the integer x/y exactly as before
    // (byte-identical).
    bool use_float_pos = false;
    // The player's sub-pixel render position for the smooth-motion HD path
    // (read by draw_player only when use_float_pos).  Lives here rather than on
    // PlayerState because PlayerState is memcpy'd whole into the POD save header
    // — render-only float fields there would change the save layout.
    float player_fx = 0.0f, player_fy = 0.0f;
};

// Scale-aware core: at scale 1 identical to the FrameBuffer path; at scale>1
// resolves the sprite through the asset cache and blits the upscaled block
// at (x*scale, y*scale).
void blit_sprite(RenderTarget& t, const formats::Sprite& s,
                 const std::vector<formats::Rgb>& pal, int x, int y,
                 bool flip_h = false);

// Sub-pixel float-position overload (HD-rounds the blit; see the keyed twin).
// Used by the smooth-motion sub-frame pass so player/entity positions lerped
// between 18Hz logic ticks advance by whole HD pixels each sub-frame.  Integer
// positions round-trip exactly so the int overload delegates here unchanged.
void blit_sprite(RenderTarget& t, const formats::Sprite& s,
                 const std::vector<formats::Rgb>& pal, float fx, float fy,
                 bool flip_h = false);

void blit_sprite(FrameBuffer& fb, const formats::Sprite& s,
                 const std::vector<formats::Rgb>& pal, int x, int y,
                 bool flip_h = false);

// Blit a sprite with majority-opaque-color transparency: the colour index
// that appears most often among opaque pixels is treated as background and
// skipped.  Used for the enhanced-mode fluid bubbles (ELEML1[17/18]) which
// are ~90% blue background with ~10% white dot detail.  The majority-vote
// mirrors the reference's background-colour detection.
void blit_sprite_keyed(RenderTarget& t, const formats::Sprite& s,
                       const std::vector<formats::Rgb>& pal, int x, int y);

// Sub-pixel float-position overload: rounds the blit at HD resolution (scale>1)
// instead of native, so slow smooth-motion sprites (fluid bubbles lerped at
// 54Hz) whose per-sub-frame native delta is < 1px still advance by whole HD
// pixels each sub-frame.  At scale 1 it rounds to the nearest native pixel.
// Integer positions round-trip exactly, so the int overload above delegates
// here with no behaviour change for any existing caller.
void blit_sprite_keyed(RenderTarget& t, const formats::Sprite& s,
                       const std::vector<formats::Rgb>& pal, float fx, float fy);

void blit_sprite_keyed(FrameBuffer& fb, const formats::Sprite& s,
                       const std::vector<formats::Rgb>& pal, int x, int y);

// `draw_player=false` skips the player (and its halo/weapon overlays) —
// used for the OUTGOING frame of a pan-scroll screen transition.  Both
// slide surfaces carrying a player shows two of them mid-pan; rendering
// the old screen player-less makes the player "ride" the incoming
// screen, matching the original's CRTC-scroll visual (reference fix:
// the _render_common_tail draw_player gate).
//
// `post_background_hook`: optional callable invoked after background +
// HUD-strip compositing but BEFORE tile placements.  Used by the enhanced-
// mode secret room to draw fluid bubbles behind the floor tiles.
// Classic path: pass nullptr (default).
// Scale-aware core: one draw-order body for any scale.  At scale 1 the
// output is byte-identical to the classic path.
void compose_frame(
    RenderTarget& t, systems::SystemsState& state,
    const LevelRenderAssets& assets, bool draw_player = true,
    const std::function<void(RenderTarget&)>& post_background_hook = nullptr);

// Background pass: PC1/fill base + HUD strip + tiles + cave sign, then sets the
// secret-screen foreground clip line.  Factored out of compose_frame so the
// enhanced widescreen present can compose an entity-free center, assemble the
// wide buffer (margins = neighbour terrain), then draw the foreground ONCE over
// the wide buffer at origin_x = margin so entities crossing the 320 edge spill
// into the margins (the kept overflow).  Both phases also run via compose_frame
// for the classic/non-widescreen paths.
void draw_background(
    RenderTarget& t, systems::SystemsState& state,
    const LevelRenderAssets& assets,
    const std::function<void(RenderTarget&)>& post_background_hook = nullptr);

// Foreground pass: entities + secret spring + hazards/popups + death halo +
// player (+ weapon overlay).  Factored out of compose_frame so the enhanced
// widescreen overflow pass can draw the live foreground over an already-
// assembled wide buffer at t.origin_x = margin (entities crossing the 320 edge
// then spill into the neighbour-terrain margins instead of being hard-clipped).
//
// Player/weapon STATE mutations (club_flag swing decrement, death/cave-warp
// club_flag clear) honour t.advance_state (see RenderTarget): the widescreen
// overflow pass sets it FALSE so its draw is purely visual, while the single
// authoritative advance fires exactly once per gameplay frame on the main fb
// compose (advance_state true there).
void draw_entities(
    RenderTarget& t, systems::SystemsState& state,
    const LevelRenderAssets& assets, bool draw_player = true);

// Reflect the animated lava-bubble entities (ObjType PteriyakiL7, L7) across a
// no-neighbour level edge into the widescreen margin, so they continue onto the
// mirrored lava floor.  Same reflection the static-bg ground_fill uses (flipped,
// across col 0 / col 319); float position → inherits the smooth-motion path.
// No-op on levels without lava bubbles.  `mirror_left`/`mirror_right` = the
// side(s) with no neighbour (left for the first screen, right for the last).
void draw_mirrored_lava_bubbles(
    RenderTarget& t, const systems::SystemsState& state,
    const LevelRenderAssets& assets, bool mirror_left, bool mirror_right);

// Compat: classic 320×200 path over a FrameBuffer.
void compose_frame(
    FrameBuffer& fb, systems::SystemsState& state,
    const LevelRenderAssets& assets, bool draw_player = true,
    const std::function<void(RenderTarget&)>& post_background_hook = nullptr);

// Re-draw the level's static background tiles (the cave-sign + tile placements)
// over the current target — used by the widescreen secret-room fast path to put
// the floor tiles back ON TOP of the dynamic fluid bubbles (the bubbles draw
// BEHIND the floor; draw_background interleaves base→bubbles→tiles, but the
// cached wide bg bakes base+tiles together, so the fast path draws bubbles over
// it then redraws the tiles to restore the order).  Honours t.scale / origin_x.
void redraw_bg_tiles(RenderTarget& t, systems::SystemsState& state,
                     const LevelRenderAssets& assets);

// Cached upscaled WIDE static background (centre bg+tiles + peek margins), the
// widescreen twin of the per-screen static-bg HD cache used by draw_background.
// Returns a (320+2*margin)*scale × 200*scale RGBA buffer, composed + upscaled
// ONCE per screen and reused until the screen / margin / neighbours / backdrop /
// profile change.  The caller memcpys it then draws the dynamic sprites + HUD
// bars at HD on top (no per-frame whole-frame upscale).  `left`/`right` are the
// pre-composed native neighbour screens (null = no neighbour); `*_screen` are
// their screen indices (for cache keying); `backdrop` is the FOND extend source
// (null = self-tile).  Only valid for surface peek screens (NOT secret rooms,
// whose animated bubbles need the live whole-frame path).
const std::vector<std::uint8_t>& get_static_wide_bg_hd(
    systems::SystemsState& state, const LevelRenderAssets& assets, int scale,
    const std::string& profile, int margin,
    const FrameBuffer* left, int left_screen,
    const FrameBuffer* right, int right_screen,
    const FrameBuffer* backdrop,
    const std::vector<LevelRenderAssets::TileDraw>& left_seam = {},
    const std::vector<LevelRenderAssets::TileDraw>& right_seam = {},
    // Synthetic seam-hole fills (tile_patterns::seam_row_bridges), kept
    // SEPARATE from the straddler lists: a straddler's margin part already
    // exists in the peek WITH the neighbour's authored z-order (re-blitting
    // it buried S14's dirt-top row under its own subsurface rock), so
    // straddlers draw CENTRE-ONLY; bridges don't exist in the peek at all,
    // so they draw into the margin too.
    const std::vector<LevelRenderAssets::TileDraw>& left_bridge = {},
    const std::vector<LevelRenderAssets::TileDraw>& right_bridge = {},
    // Bumped by the caller whenever the peek buffers are REBUILT (screen
    // bind): the key can't see the peek pixels, and a neighbour's live
    // entity state (a destroyed L7 spike rock, collected food near the
    // seam) is baked into them — without the generation, a revisit served
    // the stale margin from cache.
    std::uint64_t peek_generation = 0);

// Compose the NATIVE (un-upscaled) wide static background: centre 320 bg+tiles
// assembled into a (320+2*margin)×200 RGBA buffer with the margins filled from
// neighbours / backdrop / self-tile and the no-neighbour layer extension (the
// shared core of get_static_wide_bg_hd, which upscales + caches this).  The L3
// trunk-descent reuses it raw to fill the descent margins with the destination
// screen's static bg instead of pillarbox bars.  `out` is resized as needed.
void compose_static_wide_bg_native(
    systems::SystemsState& state, const LevelRenderAssets& assets, int margin,
    const FrameBuffer* left, const FrameBuffer* right,
    const FrameBuffer* backdrop, std::vector<std::uint8_t>& out,
    // Neighbour seam-straddling tiles (tile_patterns::seam_straddling_tiles,
    // in the NEIGHBOUR's own coords): the left neighbour's x=320-straddling
    // tiles and the right neighbour's x=0-straddling tiles, re-blitted at ∓320
    // so a trunk/pillar/bush peeked across a seam is completed whole.  Empty
    // lists = no completion (levels without straddling tiles).
    const std::vector<LevelRenderAssets::TileDraw>& left_seam = {},
    const std::vector<LevelRenderAssets::TileDraw>& right_seam = {},
    const std::vector<LevelRenderAssets::TileDraw>& left_bridge = {},
    const std::vector<LevelRenderAssets::TileDraw>& right_bridge = {});

// L1 mid-air-island END screen: continue the lake's water tile to the right of
// the island (the no-neighbour margin / centre void / panorama off-level slot).
// Self-gating (no-op unless L1's last screen / level-complete pseudo-exit).
// `origin_x` places the screen's x=0 in `wide`; `buf_w` is the host buffer
// width.  Steady/fade margin buffer: origin_x = margin, buf_w = 320+2*margin.
// Screen-transition panorama strip: origin_x = the screen's slot base, buf_w =
// the strip width.  Called by compose_static_wide_bg_native, AGAIN after the
// level-end fade's 320 centre overlay, and from the 17→18 panorama pan.
void continue_l1_end_water(const systems::SystemsState& state,
                           const LevelRenderAssets& assets, int origin_x,
                           int buf_w, std::vector<std::uint8_t>& wide);

}  // namespace olduvai::presentation
