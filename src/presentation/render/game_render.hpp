// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Game frame composition into an RGBA framebuffer: background -> background
// tiles -> tile placements -> entities -> hazards/popups -> player (+ weapon).

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "enhance/canvas.hpp"
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
    // Top 9-row label strip (RGBA, colour-keyed to alpha 0) for levels whose
    // background does not bake the labels, and caves.
    std::vector<std::uint8_t> hud_strip;      // 320*9*4 or empty
    int bg_fill_index = -1;   // palette fill when no visual bg (-1 = black)
    // Enhanced: skip the pre-baked banner sprites (GET READY 132/133, NOT
    // ENOUGH FOOD 82/91); vector text in the overlay replaces them.
    bool enhanced_vector_banners = false;
    // Enhanced: continue the backdrop through the HUD strip (rows 0-8) instead
    // of the EXE black strip.
    bool extend_top_backdrop = false;
    // Leading `tiles` entries that are bind-injected backdrop rows (L3
    // pine/trunk, L7 lavarock/cave), not level tiles.  The widescreen seam pass
    // redraws tiles[backdrop_tile_count..] over a neighbour's overhang, so the
    // centre's own tiles keep their z-order.
    int backdrop_tile_count = 0;
};

// RGBA frame buffer, 320x200 by default; (w,h) for HD or wide buffers.
struct FrameBuffer {
    int w = 320;
    int h = 200;
    std::vector<std::uint8_t> px =
        std::vector<std::uint8_t>(320 * 200 * 4, 0);
    FrameBuffer() = default;
    FrameBuffer(int w_, int h_) : w(w_), h(h_),
        px(static_cast<std::size_t>(w_) * h_ * 4, 0) {}
    enhance::Canvas canvas() { return {px, w, h}; }
};

// Byte offset of (x, y).  Not always 320 wide (the widescreen pause buffer is
// native_w()); never stride by a literal 320.
inline std::size_t fb_off(const FrameBuffer& fb, int x, int y) {
    return (static_cast<std::size_t>(y) * fb.w + x) * 4;
}

// Blit/compose target.  scale 1: plain 320x200 indexed blit.  scale > 1: HD
// buffer composed from cached per-asset upscales.
struct RenderTarget {
    std::uint8_t* px = nullptr;   // w*h*4 RGBA, caller-owned
    int w = 320;
    int h = 200;
    int scale = 1;
    enhance::HdAssetCache* cache = nullptr;   // null at scale 1
    const std::string* profile = nullptr;     // null at scale 1
    // The HD path needs cache and profile.  Branch on this, never on scale
    // alone, before reading them.
    bool hd_path() const {
        return scale > 1 && cache != nullptr && profile != nullptr;
    }
    // Bottom clip in scaled dst-y (exclusive).  The secret screen sets
    // (screen_height+1)*scale so foreground sprites stop at the floor: the
    // EXE's EGA-path clamp that its VGA path omits (FUN_1052_2813).
    int clip_y = 1 << 28;
    // Horizontal clip in scaled dst-x [lo, hi).  The widescreen no-neighbour
    // pass draws bg tiles unclipped into that margin (the L3 end trunk runs to
    // x=384) while protecting the opposite margin's peek.
    int clip_x_lo = -(1 << 28);
    int clip_x_hi = 1 << 28;
    // Player-only clip: keeps the player off a no-neighbour edge fill while
    // flying entities (the bird) still overflow.
    int player_clip_x_lo = -(1 << 28);
    int player_clip_x_hi = 1 << 28;
    // Native dst-x bias for every blit.  The widescreen overflow pass sets it
    // to the margin so entities crossing 320 spill into the margins (enhanced
    // only).
    int origin_x = 0;
    // false: draw_entities draws but skips the draw-time state changes
    // (club_flag swing decrement, death/cave-warp clear).  The widescreen
    // overflow pass and sub-frame re-renders set it; the main fb compose
    // advances once per tick.
    bool advance_state = true;
    // HD smooth sub-frame: read Entity::fx/fy and player_fx/fy and round at HD
    // (1 HD px steps, not the integer position's scale-px snap).
    bool use_float_pos = false;
    // Player sub-pixel render position (use_float_pos only).  Not on
    // PlayerState, which is memcpy'd into the POD save header.
    float player_fx = 0.0f, player_fy = 0.0f;
};

// Decode a sprite to RGBA as the HD path does: palette lookup, magenta for an
// out-of-range index, flip applied first, transparent pixels zero.
// HdAssetCache keys on these bytes, so the pre-warm (hd_warm.hpp) must produce
// exactly them; tests/test_hd_warm.cpp checks the agreement against a real
// blit.
std::vector<std::uint8_t> sprite_to_rgba(const formats::Sprite& s,
                                         const std::vector<formats::Rgb>& pal,
                                         bool flip_h);

// Palette-indexed pixels to opaque RGBA, the first `count` of them into `out`
// (4 bytes each).  An index past the palette is black.
void indexed_to_rgba(const std::vector<std::uint8_t>& pixels,
                     const std::vector<formats::Rgb>& pal, std::uint8_t* out,
                     std::size_t count);

// A PC1 as a 320x200 frame, in its own palette.  Clamped: a malformed PC1 can
// declare more than 200 rows.
FrameBuffer pc1_frame(const formats::Pc1Image& img);

// The balloon bunch (L1SPR.MAT): the death halo and the enhanced fly-away.
constexpr int kSprBalloonBunch = 117;

// scale 1: indexed blit.  scale > 1: the sprite's cached upscale at
// (x*scale, y*scale).
void blit_sprite(RenderTarget& t, const formats::Sprite& s,
                 const std::vector<formats::Rgb>& pal, int x, int y,
                 bool flip_h = false);

// Float position, rounded at HD, for smooth-motion sub-frames.  Integer
// positions round-trip exactly.
void blit_sprite(RenderTarget& t, const formats::Sprite& s,
                 const std::vector<formats::Rgb>& pal, float fx, float fy,
                 bool flip_h = false);

void blit_sprite(FrameBuffer& fb, const formats::Sprite& s,
                 const std::vector<formats::Rgb>& pal, int x, int y,
                 bool flip_h = false);

// Blit treating the most frequent opaque colour as transparent (the reference's
// background detection).  For the enhanced fluid bubbles (ELEML1[17/18]):
// ~90% blue background.
void blit_sprite_keyed(RenderTarget& t, const formats::Sprite& s,
                       const std::vector<formats::Rgb>& pal, int x, int y);

// Float position, rounded at HD (at scale 1, to the nearest native pixel):
// slow bubbles still move every sub-frame.
void blit_sprite_keyed(RenderTarget& t, const formats::Sprite& s,
                       const std::vector<formats::Rgb>& pal, float fx, float fy);

void blit_sprite_keyed(FrameBuffer& fb, const formats::Sprite& s,
                       const std::vector<formats::Rgb>& pal, int x, int y);

// `draw_player=false`: no player (or halo/weapon), for a pan's outgoing frame,
// so the player rides the incoming screen.
// `post_background_hook`: called after background + HUD strip, before tiles
// (enhanced secret-room bubbles behind the floor).
void compose_frame(
    RenderTarget& t, systems::SystemsState& state,
    const LevelRenderAssets& assets, bool draw_player = true,
    const std::function<void(RenderTarget&)>& post_background_hook = nullptr);

// Background pass: base + HUD strip + tiles + cave sign, then the secret-screen
// floor clip.  Separate so the widescreen present can draw the foreground once
// over the assembled wide buffer.
void draw_background(
    RenderTarget& t, systems::SystemsState& state,
    const LevelRenderAssets& assets,
    const std::function<void(RenderTarget&)>& post_background_hook = nullptr);

// Foreground pass: entities, secret spring, hazards/popups, death halo, player
// (+ weapon).  State changes honour t.advance_state.
void draw_entities(
    RenderTarget& t, systems::SystemsState& state,
    const LevelRenderAssets& assets, bool draw_player = true);

// Reflect the L7 lava-bubble entities across a no-neighbour edge into the
// margin, onto the mirrored lava (flipped across col 0 / 319).  No-op on other
// levels.
void draw_mirrored_lava_bubbles(
    RenderTarget& t, const systems::SystemsState& state,
    const LevelRenderAssets& assets, bool mirror_left, bool mirror_right);

// Compat: classic 320x200 path over a FrameBuffer.
void compose_frame(
    FrameBuffer& fb, systems::SystemsState& state,
    const LevelRenderAssets& assets, bool draw_player = true,
    const std::function<void(RenderTarget&)>& post_background_hook = nullptr);

// Redraw the static bg tiles (cave sign + placements).  The widescreen
// secret-room fast path draws bubbles over the cached bg, then this puts the
// floor back on top.
void redraw_bg_tiles(RenderTarget& t, systems::SystemsState& state,
                     const LevelRenderAssets& assets);

// Peek state for get_static_wide_bg_hd (most of it WidescreenPresenter
// members).
struct WidePeek {
    // Pre-composed neighbour screens (null = none); the indices are part of the
    // cache key.
    const FrameBuffer* left = nullptr;
    int left_screen = 0;
    const FrameBuffer* right = nullptr;
    int right_screen = 0;
    // FOND extend source (null = self-tile).
    const FrameBuffer* backdrop = nullptr;
    // Neighbour straddling tiles, drawn centre-only: their margin part is
    // already in the peek with the right z-order.
    const std::vector<LevelRenderAssets::TileDraw>* left_seam = nullptr;
    const std::vector<LevelRenderAssets::TileDraw>* right_seam = nullptr;
    // Seam-hole fills (seam_row_bridges): not in the peek, so they draw into
    // the margin too.
    const std::vector<LevelRenderAssets::TileDraw>* left_bridge = nullptr;
    const std::vector<LevelRenderAssets::TileDraw>* right_bridge = nullptr;
    // Bumped whenever the peek buffers are rebuilt (their content is not in the
    // cache key).
    std::uint64_t generation = 0;
};

// Upscaled wide static background (centre bg + tiles + margins),
// (320+2*margin)*scale x 200*scale, cached per screen / margin / neighbours /
// backdrop / profile.  The caller copies it and draws sprites and HUD bars on
// top.  Surface peek screens only (secret-room bubbles need the live path).
const std::vector<std::uint8_t>& get_static_wide_bg_hd(
    systems::SystemsState& state, const LevelRenderAssets& assets, int scale,
    const std::string& profile, int margin, const WidePeek& peek);

// Neighbour tiles re-blitted across a seam so a straddling object is drawn
// whole: the left neighbour's x=320 straddlers at -320, the right's x=0 ones
// at +320 (neighbour coordinates).  Empty = none.
struct SeamTiles {
    using Tiles = std::vector<LevelRenderAssets::TileDraw>;
    static const Tiles kNone;
    const Tiles& left = kNone;
    const Tiles& right = kNone;
    const Tiles& left_bridge = kNone;
    const Tiles& right_bridge = kNone;
};

// Native wide static background: centre bg + tiles in a (320+2*margin)x200
// RGBA buffer, margins from neighbours / backdrop / self-tile plus the
// no-neighbour layer extension.  The core of get_static_wide_bg_hd; the L3
// descent uses it directly for its margins.
void compose_static_wide_bg_native(
    systems::SystemsState& state, const LevelRenderAssets& a, int margin,
    const FrameBuffer* left, const FrameBuffer* right,
    const FrameBuffer* backdrop, std::vector<std::uint8_t>& wide,
    SeamTiles seams = {});

// L1 end screen: continue the lake water right of the island (margin, centre
// void, panorama slot).  No-op elsewhere.  `origin_x`: where screen x=0 sits in
// `wide` (margin for the steady/fade buffer, the slot base in a panorama
// strip); `buf_w`: its width.
void continue_l1_end_water(const systems::SystemsState& state,
                           const LevelRenderAssets& assets, int origin_x,
                           int buf_w, std::vector<std::uint8_t>& wide);

}  // namespace olduvai::presentation
