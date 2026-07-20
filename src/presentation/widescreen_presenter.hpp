// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen presenter (OL-B5).
//
// Extracted from run_platform_level (game_app.cpp): the §8.7 widescreen
// presentation STATE (margin/native width/active flag, the wide SDL texture,
// the neighbour peek cache + seam lists, the Tier-1 living-margin monster
// clones, the FOND backdrop, the reusable HD wide frame) and its MACHINERY
// (margin math + Alt+Enter/resize recompute, peek-cache rebuild, the
// wrap_wide* family, reapply_seam_bands, and the steady widescreen present
// with both its HD fast path and the whole-frame slow path).
//
// The class owns everything that was per-level widescreen shell state; it is
// constructed once per run_platform_level over a WidescreenShellCtx — a
// narrow view of the shell (renderer, level-fixed presentation params, the
// live state/render aggregates, and the few callbacks that reach back into
// the TU-private `Loaded` helpers), mirroring OL-B3's TransitionShellCtx
// discipline.  BEHAVIOR-PRESERVING move: method internals are verbatim from
// game_app.cpp; the block comments moved with them.
//
// CC2d moved the rest of the wide PRESENT machinery in: the wide-transition
// present (present_transition) and the wide HUD-text mapping
// (draw_wide_hud_text; banners stay a shell hook — they are state-driven).
//
// What did NOT move (still in game_app.cpp, reaching this state through the
// accessors): the blocking transition players' ctx wiring (OL-B3), the L3
// trunk-descent presenter (welded to the descent phase sequencing + F5
// latch), upload_and_show's pillarbox branch (part of the shell's own
// general present), save/load and pause.
#pragma once

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "enhance/enhanced_hud.hpp"
#include "enhance/hd_asset_cache.hpp"
#include "enhance/hd_text.hpp"
#include "presentation/game_render.hpp"
#include "presentation/text_overlay.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::systems { struct SystemsState; }

namespace olduvai::presentation {

// Narrow shell context for the widescreen presenter.  Built ONCE per
// run_platform_level; every pointer refers to a shell object that outlives
// the presenter (the per-level locals of run_platform_level).
struct WidescreenShellCtx {
    SDL_Renderer* ren = nullptr;

    // Level-fixed presentation parameters.
    bool hd = false;
    int hd_scale = 1;
    bool use_hd_text = false;
    // Live-mutable shell strings (Tier-1 Aspect / Options edits write these):
    // opts.aspect and opts.hd_profile.  hd_profile doubles as the
    // RenderTarget::profile pointer.
    const std::string* aspect = nullptr;
    const std::string* hd_profile = nullptr;

    // Live per-level aggregates (rebound in place across screens; the
    // compose/draw functions take SystemsState by non-const ref).
    systems::SystemsState* state = nullptr;        // g.state
    const LevelRenderAssets* render = nullptr;     // g.render
    enhance::HdAssetCache* hd_cache = nullptr;     // g.hd_cache
    enhance::HdText* hd_text = nullptr;
    TextOverlay* text_overlay = nullptr;
    int internal_level_id = 0;                     // g.config.internal_id
    int surface_screen_count = 0;                  // g.tiles.screens.size()

    // Shell logical-size mirrors: the text-overlay flush restores SDL's
    // logical size from these, so the resize recompute must keep them in
    // lockstep (see rebuild_if_resized).  fallback_ld = the non-widescreen
    // (margin-0) logical dims.
    int* logical_w = nullptr;
    int* logical_h = nullptr;
    LogicalDims fallback_ld{};

    // Callbacks into the TU-private `Loaded` helpers (game_app.cpp).
    // compose_surface_screen_static(g, screen, out, ra, underlay,
    //                               frozen_full, peek_monsters).
    std::function<void(int screen, FrameBuffer& out, LevelRenderAssets* ra,
                       const std::vector<LevelRenderAssets::TileDraw>* underlay,
                       bool frozen_full, bool peek_monsters)>
        compose_static;
    // collect_spawn_post_monsters(g, screen) — Tier-1 living-margin clones.
    std::function<std::vector<core::Entity>(int screen)> collect_monsters;

    // Shell overlay hooks (assigned via the setters below once the shell
    // lambdas they wrap exist — they are defined after the presenter is
    // constructed, exactly like the old in-loop ordering):
    //   draw_overlay_tail    — draw_l3_smoke_tail (enhanced descent dust).
    //   draw_banners         — draw_enhanced_banners (level/food banner
    //                          substitutes; shell-owned, state-driven).  The
    //                          HUD-text mapping itself is presenter-internal
    //                          (draw_wide_hud_text) since CC2d.
    std::function<void(RenderTarget&)> draw_overlay_tail;
    std::function<void(std::vector<std::uint8_t>& b, int ow, int oh)>
        draw_banners;
};

class WidescreenPresenter {
public:
    // Computes the margin from the CURRENT renderer output size and creates
    // the wide texture when active — the level-entry half of the old inline
    // block.  The level-derived state (peek cache, FOND backdrop) is built
    // by the explicit update_cache()/build_backdrop() calls at the original
    // level-entry sites.
    explicit WidescreenPresenter(WidescreenShellCtx ctx);
    ~WidescreenPresenter();
    WidescreenPresenter(const WidescreenPresenter&) = delete;
    WidescreenPresenter& operator=(const WidescreenPresenter&) = delete;

    // Late hook wiring (the shell lambdas are defined after construction).
    void set_draw_overlay_tail(std::function<void(RenderTarget&)> fn);
    void set_draw_banners(
        std::function<void(std::vector<std::uint8_t>&, int, int)> fn);

    // Vector HUD text + banners over the centre 320 sub-region of a WIDE
    // output — the ONE mapping shared by present(), present_transition()
    // and upload_and_show's pillarboxed-WS branch (which calls it from the
    // shell).  Moved verbatim from run_platform_level (CC2d).
    void draw_wide_hud_text(std::vector<std::uint8_t>& b, int ow, int oh,
                            const enhance::EnhancedHudLayout& L);

    // ── State accessors (the shell paths that stayed behind read these) ──
    bool active() const { return active_; }
    int margin() const { return margin_; }
    int native_w() const { return native_w_; }
    SDL_Texture* wide_tex() const { return wtex_; }
    bool left_ok() const { return left_ok_; }
    bool right_ok() const { return right_ok_; }
    bool backdrop_ok() const { return backdrop_ok_; }
    const FrameBuffer& backdrop() const { return backdrop_; }

    // ONE authoritative present-path predicate: widescreen runs (peek OR
    // secret self-tile).  Selects present() (composes its own wide buffer
    // with the entity-overflow pass) over upload_and_show — this SAME
    // predicate gates every present call site, so the "is the overflow pass
    // running this frame?" decision cannot diverge between sites.
    bool present_path() const;

    // Recompute widescreen state when the renderer output size changes
    // (Alt+Enter fullscreen toggle / window resize).  Cheap no-op when
    // unchanged.  Rebuilds the level-derived state when widescreen turns ON
    // mid-level (the old ws_refresh_on_activate wiring, now internal).
    void rebuild_if_resized();

    // Rebuild the neighbour peek cache (+ seam lists + living-margin monster
    // clones) for the CURRENT screen.  Call after every screen bind.
    void update_cache();

    // Build (or rebuild) the pure-FOND backdrop for the CURRENT level.
    void build_backdrop();

    // Tier-1 living margins: cycle the peek monsters' walk sprites IN PLACE,
    // once per logic tick (tick site stays in the run loop).
    void tick_margin_monsters();

    // Part 1 smooth plumbing: the smooth sub-frame caller sets the float
    // render position around its present so the overflow draw reads fx/fy.
    void set_float_pos(bool use, float fx = 0.0f, float fy = 0.0f);

    // ── Widescreen present (§8.7, Option A) — steady-frame present with the
    // HD fast path (cached static wide bg) and the whole-frame slow path.
    void present(const std::function<void(RenderTarget&)>& bubble_hook_w,
                 bool do_present = true);

    // ── Wide-transition present (§8.7 wide transitions): present a ready-
    // made WIDE native buffer (native_w()×200, no baked HUD) through the
    // wide texture, exactly like present()'s tail — fixed HUD over the
    // centre 320 so width and HUD position stay continuous with the steady
    // widescreen frame.  Moved verbatim from run_platform_level (CC2d).
    void present_transition(std::vector<std::uint8_t>& wide,
                            bool with_hud = true, bool pre_upscaled = false);

    // ── Wide-buffer wraps for the transition/fade paths (stay in the shell,
    // sequenced around the cache rebuild there) ──
    void wrap_wide(const FrameBuffer& center, std::vector<std::uint8_t>& out);
    void wrap_wide_bezel(const FrameBuffer& center,
                         std::vector<std::uint8_t>& out);
    void wrap_wide_for(const FrameBuffer& center, bool is_present,
                       std::vector<std::uint8_t>& out);
    void wrap_wide_static(const FrameBuffer& center,
                          std::vector<std::uint8_t>& out);

    // Re-apply the neighbour seam content (straddler completions + row
    // bridges) that a raw 320-centre memcpy just buried.  Shared with the L3
    // trunk-descent presenter (which stays in the shell).
    void reapply_seam_bands(std::vector<std::uint8_t>& wide);

    // Compose the CURRENT screen's static wide background (no centre
    // overlay) from the presenter's cache — the descent margins builder.
    void compose_static_wide_bg(std::vector<std::uint8_t>& out);

private:
    int compute_margin(int ow, int oh) const;
    bool secret_selftile() const;
    void refresh_level_state();   // build_backdrop + update_cache
    void draw_margin_monsters(RenderTarget& wrt);

    WidescreenShellCtx ctx_;

    // Last renderer output size the widescreen state was computed for; the
    // Alt+Enter/resize recompute compares against it.
    int ow0_ = 0, oh0_ = 0;
    // Mutable: Alt+Enter fullscreen toggle / window resize recomputes the
    // margin (and so active_/native_w_/logical size) via rebuild_if_resized.
    int margin_ = 0;
    bool active_ = false;
    int native_w_ = 320;   // wide native width (320 + 2*margin)
    SDL_Texture* wtex_ = nullptr;   // wide texture (lazy; recreated on resize)

    // ── Widescreen neighbor cache (§8.7) ─────────────────────────────────
    // STATIC per-screen (bg + terrain, no entities, no RNG), composed once on
    // screen-bind and reused every frame.
    FrameBuffer left_, right_;
    bool left_ok_ = false, right_ok_ = false;
    int left_screen_ = -1, right_screen_ = -1;   // wide-bg cache key
    // Seam-column completion (tile_patterns) + authored seam-hole bridges.
    std::vector<LevelRenderAssets::TileDraw> left_seam_, right_seam_,
        left_bridge_, right_bridge_;
    // Tier-1 living margins: animated spawn-post monster clones for the two
    // peeks (sprite-cycled in place once per logic tick; drawn live over the
    // cached static bg — which excludes them).
    std::vector<core::Entity> left_mons_, right_mons_;
    // Bumped on every peek rebuild — keys the wide static-bg HD cache to the
    // peek CONTENT (see get_static_wide_bg_hd).
    std::uint64_t peek_generation_ = 0;
    // Reusable per-frame HD wide buffer (fast widescreen present).
    std::vector<std::uint8_t> frame_hd_;

    // Pure-FOND backdrop (320×200 RGBA) for the no-neighbour margin
    // extension.  Built once per level (background is level-stable).
    FrameBuffer backdrop_;
    bool backdrop_ok_ = false;

    // Part 1: set by the smooth sub-frame caller so present()'s own overflow
    // draw_entities reads the float render positions (fx/fy); false on every
    // other call (screenshot, non-smooth present) → integer path.
    bool use_float_pos_ = false;
    float player_fx_ = 0.0f, player_fy_ = 0.0f;
};

}  // namespace olduvai::presentation
