// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen presenter: margin math and resize recompute, the wide texture,
// the neighbour peek cache and seam lists, the living-margin monsters, the FOND
// backdrop, the wrap_wide* family and the steady / transition wide presents.
// Built once per run_platform_level over a WidescreenShellCtx.
#pragma once

#include "presentation/render/level_surface.hpp"
#include "presentation/render/logical_size.hpp"

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "enhance/enhanced_hud.hpp"
#include "enhance/hd_asset_cache.hpp"
#include "enhance/hd_text.hpp"
#include "presentation/render/dirty_frame.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/render/text_overlay.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::systems { struct SystemsState; }

namespace olduvai::presentation {

struct FrameStats;

// Shell view for the presenter, built once per level; every pointer outlives
// the presenter.
struct WidescreenShellCtx {
    // Renderer, texture, vector font, overlay, logical size and HD settings.
    LevelSurface* surface = nullptr;

    // The live Aspect setting (a widescreen concern, not the surface's).
    const std::string* aspect = nullptr;

    // Live per-level aggregates, rebound in place across screens.
    systems::SystemsState* state = nullptr;        // g.state
    const LevelRenderAssets* render = nullptr;     // g.render
    enhance::HdAssetCache* hd_cache = nullptr;     // g.hd_cache
    int internal_level_id = 0;                     // g.config.internal_id
    int surface_screen_count = 0;                  // g.tiles.screens.size()
    // The LEVEL has a FOND backdrop.  Not render->visual_background, which is
    // the current screen's (false in caves / secret rooms).
    bool level_visual_background = false;

    // Callback into game_app's TU-private helpers:
    // compose_surface_screen_static.
    std::function<void(int screen, FrameBuffer& out, LevelRenderAssets* ra,
                       const std::vector<LevelRenderAssets::TileDraw>* underlay,
                       bool frozen_full, bool peek_monsters)>
        compose_static;
    // collect_spawn_post_monsters(g, screen) — Tier-1 living-margin clones.
    std::function<std::vector<core::Entity>(int screen)> collect_monsters;

    // Shell overlay hooks, set after construction: draw_overlay_tail (L3 dust,
    // teleport clouds, balloons) and draw_banners (state-driven banner
    // substitutes).
    std::function<void(RenderTarget&)> draw_overlay_tail;
    std::function<void(const enhance::Canvas&)> draw_banners;
    // Banner overlay key; 0 = animating, never skip the overlay.  Unset counts
    // as 0, so skipping is opt-in.
    std::function<std::uint64_t()> banners_key;
};

class WidescreenPresenter {
public:
    // Computes the margin from the current output size and creates the wide
    // texture if active.  Level-derived state is built by update_cache() /
    // build_backdrop().
    explicit WidescreenPresenter(WidescreenShellCtx ctx);
    ~WidescreenPresenter() = default;
    WidescreenPresenter(const WidescreenPresenter&) = delete;
    WidescreenPresenter& operator=(const WidescreenPresenter&) = delete;

    // Late hook wiring (the shell lambdas are defined after construction).
    void set_draw_overlay_tail(std::function<void(RenderTarget&)> fn);
    void set_banners_key(std::function<std::uint64_t()> fn);
    void set_draw_banners(std::function<void(const enhance::Canvas&)> fn);

    // Vector HUD text + banners over the centre 320 of a wide output; the one
    // mapping for present(), present_transition() and the pillarboxed path.
    std::uint64_t overlay_key(const enhance::EnhancedHudLayout& L,
                              int ow, int oh) const;
    void draw_wide_hud_text(const enhance::Canvas& cv,
                            const enhance::EnhancedHudLayout& L) const;

    // Wide foreground: entities, unclip + L7 lava-bubble reflection, overlay
    // tail, margin monsters, on a foreground_target after clip_foreground.
    void draw_wide_foreground(RenderTarget& wrt);

    // Clear, copy the wide texture across the canvas, draw the vector HUD
    // text (none for a null layout).  Caller must have filled the texture.
    void show_wide_with_hud(const enhance::EnhancedHudLayout* hud) const;

    // ── State accessors (the shell paths that stayed behind read these) ──

    // Surface reads.
    SDL_Renderer* ren() const { return ctx_.surface->ren(); }
    bool hd() const { return ctx_.surface->hd(); }
    int hd_scale() const { return ctx_.surface->hd_scale(); }
    bool use_hd_text() const { return ctx_.surface->use_hd_text(); }
    const std::string* hd_profile() const { return ctx_.surface->hd_profile(); }
    enhance::HdText& hd_text() const { return ctx_.surface->hd_text(); }
    LogicalSize& lsz() const { return ctx_.surface->lsz(); }

    bool active() const { return active_; }
    int margin() const { return margin_; }
    int native_w() const { return native_w_; }
    // The Aspect setting, for the F5 report: active()/margin() alone cannot
    // tell "display too narrow" from "widescreen not chosen".
    const std::string& aspect() const { return *ctx_.aspect; }
    // The surface's wide texture at this width; null when inactive.
    SDL_Texture* wide_tex() const {
        return active_ ? ctx_.surface->wide_tex(native_w_) : nullptr;
    }
    bool left_ok() const { return left_ok_; }
    bool right_ok() const { return right_ok_; }
    bool backdrop_ok() const { return backdrop_ok_; }
    const FrameBuffer& backdrop() const { return backdrop_; }

    // The one present-path predicate (peek, secret self-tile, no-neighbour
    // surface fill), shared by every present call site.
    bool present_path() const;

    // Before every present: follow the output size (Alt+Enter, resize) and
    // the Aspect setting, which the pause edits live.  The logical size is
    // derived here and nowhere else; a no-op when neither changed.  Builds
    // the level-derived state when widescreen turns on mid-level.
    void sync_output();

    // Rebuild the peek cache, seam lists and margin monsters for the current
    // screen.  Call after every screen bind.
    void update_cache();

    // Build (or rebuild) the pure-FOND backdrop for the CURRENT level.
    void build_backdrop();

    // Margin monsters: cycle walk sprites in place, once per logic tick.
    void tick_margin_monsters();

    // Set around a smooth sub-frame present so the overflow draw reads fx/fy.
    void set_float_pos(bool use, float fx = 0.0f, float fy = 0.0f);

    // Steady present: HD fast path (cached static wide bg) or whole-frame slow
    // path.
    void present(const std::function<void(RenderTarget&)>& bubble_hook_w,
                 bool do_present = true);

    // Present a wide native buffer (no baked HUD) with the HUD fixed over the
    // centre 320, so transitions keep the steady frame's width and HUD
    // position.  do_present=false leaves the frame for a readback; flip()
    // then presents it.
    void present_transition(std::vector<std::uint8_t>& wide,
                            bool with_hud = true, bool pre_upscaled = false,
                            bool do_present = true) const;
    // OLDUVAI_DUMP_OUTPUT, then the flip, timed as the vsync block: presents
    // what a do_present=false call left.
    void flip() const;

    // Wide-buffer wraps for the transition and fade paths.
    void wrap_wide(const FrameBuffer& center, std::vector<std::uint8_t>& out);
    void wrap_wide_bezel(const FrameBuffer& center,
                         std::vector<std::uint8_t>& out) const;
    void wrap_wide_for(const FrameBuffer& center, bool is_present,
                       std::vector<std::uint8_t>& out);
    void wrap_wide_static(const FrameBuffer& center,
                          std::vector<std::uint8_t>& out);

    // Re-apply seam content (straddlers, row bridges) covered by a 320 centre
    // copy.  centre_has_entities: the centre is a composed frame, so redraw its
    // entities after the tile pass.
    void reapply_seam_bands(std::vector<std::uint8_t>& wide,
                            bool centre_has_entities = false,
                            bool draw_player = false);

    // The current screen's static wide background (no centre overlay), for the
    // descent margins.
    void compose_static_wide_bg(std::vector<std::uint8_t>& out);

    // OLDUVAI_FRAME_STATS: the same counters as FramePresenter.  Widescreen
    // (what the handheld port ships) presents here, not through fp.present().
    // present_ms spans compose + present, so the foreground, scene and glyph
    // passes time their own buckets.  No-op when the env var is unset.
    FrameStats* stats = nullptr;    // &diag.stats

private:
    // present()'s two paths: the HD cached-background compose, and the
    // whole-frame upscale when the wide texture is missing.  Both fill the
    // wide texture.
    void present_fast(const std::function<void(RenderTarget&)>& bubbles,
                      const enhance::EnhancedHudLayout* hud);
    void present_slow(const std::function<void(RenderTarget&)>& bubbles,
                      const enhance::EnhancedHudLayout* hud);
    // The foreground target over a wide buffer: origin at the margin, no
    // state advance, the smooth float position.
    RenderTarget foreground_target(std::uint8_t* px, int w, int h, int scale,
                                   enhance::HdAssetCache* cache,
                                   const std::string* profile) const;
    // Its clips, after any bubble pass: the secret room's floor and the
    // player-only clip at a no-neighbour margin.
    void clip_foreground(RenderTarget& wrt) const;
    int compute_margin(int ow, int oh) const;
    // The FOND backdrop for the wide composers, or nullptr when this screen has
    // none.
    const FrameBuffer* ws_backdrop() const;
    bool secret_selftile() const;
    bool surface_selffill() const;
    void refresh_level_state();   // build_backdrop + update_cache
    void draw_margin_monsters(RenderTarget& wrt);

    WidescreenShellCtx ctx_;

    // Output size the state was last computed for.
    int ow0_ = 0, oh0_ = 0;
    std::string applied_aspect_;   // the Aspect sync_output last applied
    void note_no_margin_(int ow, int oh);
    int margin_ = 0;
    bool said_no_margin_ = false;   // the one-time margin warning, printed at most once
    bool active_ = false;
    int native_w_ = 320;   // wide native width (320 + 2*margin)

    // Neighbour peeks: static per screen (bg + terrain, no entities, no RNG).
    FrameBuffer left_, right_;
    bool left_ok_ = false, right_ok_ = false;
    int left_screen_ = -1, right_screen_ = -1;   // wide-bg cache key
    // Seam-column completion (tile_patterns) + authored seam-hole bridges.
    std::vector<LevelRenderAssets::TileDraw> left_seam_, right_seam_,
        left_bridge_, right_bridge_;
    // Animated margin monster clones, drawn live over the cached bg (which
    // excludes them).
    std::vector<core::Entity> left_mons_, right_mons_;
    // Bumped on every peek rebuild; keys the wide static-bg HD cache to the
    // peek content.
    std::uint64_t peek_generation_ = 0;
    // The fast present's HD wide frame, kept between presents and repainted
    // only where it changed (dirty_frame.hpp); `hud_rects_` are the bars'.
    DirtyFrame dirty_;
    std::vector<DirtyRect> hud_rects_;

    // FOND backdrop (320x200 RGBA), built once per level.
    FrameBuffer backdrop_;
    bool backdrop_ok_ = false;

    // Set by the smooth sub-frame caller: the overflow draw reads fx/fy.
    bool use_float_pos_ = false;
    float player_fx_ = 0.0f, player_fy_ = 0.0f;
};

}  // namespace olduvai::presentation
