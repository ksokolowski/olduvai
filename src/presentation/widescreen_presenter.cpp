// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen presenter (OL-B5) — implementation.  BEHAVIOR-PRESERVING move:
// every method body below is verbatim from run_platform_level (game_app.cpp)
// modulo the mechanical renames (ws_margin → margin_, g.state → *ctx_.state,
// the compose helpers → ctx_ callbacks).  The block comments moved with the
// code they document.

#include "presentation/widescreen_presenter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "core/constants.hpp"
#include "enhance/upscale.hpp"
#include "presentation/boss_widescreen.hpp"   // boss_ws_margin (shared margin math)
#include "presentation/image_out.hpp"
#include "presentation/tile_patterns.hpp"
#include "presentation/widescreen.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

namespace {
// Debug aid: OLDUVAI_DUMP_STEADY=<dir> saves every STEADY widescreen
// present as a BMP (the wide composite buffer, pre-text-overlay).  The
// companion of OLDUVAI_DUMP_TRANSITION — together they cover everything a
// widescreen session shows, so per-tick effects (cave-emerge stages,
// teleport poses) can be verified frame-exactly in headless runs.
void dump_steady_wide(const std::uint8_t* px, int w, int h) {
    const char* dir = std::getenv("OLDUVAI_DUMP_STEADY");
    if (dir == nullptr) return;
    static int seq = 0;
    char path[512];
    std::snprintf(path, sizeof path, "%s/steady_ws_%04d.bmp", dir, seq++);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<std::uint8_t*>(px), w, h, 32, w * 4,
        SDL_PIXELFORMAT_RGBA32);
    save_surface_image(s, path);
    SDL_FreeSurface(s);
}
}  // namespace

// ── Widescreen adjacent-screen peek (§8.7), enhanced-only ──────────────
// Option A: compose the center at NATIVE 320×200, assemble the wide native
// buffer via compose_widescreen, then upscale the WHOLE wide buffer.  (v1
// tradeoff: in widescreen the center goes through the whole-frame upscaler
// rather than the per-asset-HD path; per-asset-HD center is a follow-up.)
// Margin M is derived from the renderer output aspect.  Shared with the boss
// path via boss_ws_margin (same desired = 200*ow/oh; m = (desired-320)/2; cap
// 0..120; OLDUVAI_WS_FORCE_MARGIN override).  A method so BOTH level entry
// AND the Alt+Enter/resize recompute (rebuild_if_resized below) use exactly
// the same math.  Emits the ultrawide-cap warning once (boss_ws_margin caps
// silently, so the warn condition is recomputed on the uncapped margin here).
int WidescreenPresenter::compute_margin(int ow, int oh) const {
    if (*ctx_.aspect != "widescreen" || !ctx_.hd || ow <= 0 || oh <= 0)
        return 0;
    const char* fm = std::getenv("OLDUVAI_WS_FORCE_MARGIN");
    if (fm == nullptr) {
        // Widescreen is tuned/validated for ~16:9..21:9 (m up to ~73).
        // ULTRAWIDE (32:9 etc.) is UNTESTED/UNSUPPORTED: past the cap the
        // wide buffer no longer fills the display, so aspect-preserved
        // presentation simply restores side bars — graceful, not pathological
        // (see Finding widescreen_peek_*).
        const int desired = static_cast<int>(
            std::lround(200.0 * static_cast<double>(ow) / oh));
        if ((desired - 320) / 2 > 120) {
            static bool warned = false;
            if (!warned) {
                std::fprintf(stderr,
                    "widescreen: display aspect wider than the tuned "
                    "16:9..21:9 range; peek margins capped, side bars may "
                    "remain (ultrawide is untested/unsupported).\n");
                warned = true;
            }
        }
    }
    // OLDUVAI_WS_FORCE_MARGIN pins the margin regardless of window aspect: on
    // a 16:10 panel (Mac built-in) the derived margin is 0 (widescreen
    // INACTIVE per §8.7), so headless smoke can never exercise the wide
    // present/transition path without it.  Production (no env) is unchanged.
    return boss_ws_margin(ow, oh, fm);
}

WidescreenPresenter::WidescreenPresenter(WidescreenShellCtx ctx)
    : ctx_(std::move(ctx)) {
    SDL_GetRendererOutputSize(ctx_.ren, &ow0_, &oh0_);
    margin_ = compute_margin(ow0_, oh0_);
    active_ = (*ctx_.aspect == "widescreen") && ctx_.hd && margin_ > 0;
    native_w_ = 320 + 2 * margin_;   // wide native width
    // The classic streaming texture (owned by the shell) stays 320*hd_scale
    // wide for EVERY non-widescreen path — unchanged.  Widescreen present
    // uses its own WIDE texture wtex_.
    if (active_) {
        wtex_ = SDL_CreateTexture(ctx_.ren, SDL_PIXELFORMAT_RGBA32,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  native_w_ * ctx_.hd_scale,
                                  200 * ctx_.hd_scale);
    }
}

WidescreenPresenter::~WidescreenPresenter() {
    if (wtex_ != nullptr) SDL_DestroyTexture(wtex_);
}

void WidescreenPresenter::set_draw_overlay_tail(
    std::function<void(RenderTarget&)> fn) {
    ctx_.draw_overlay_tail = std::move(fn);
}

void WidescreenPresenter::set_draw_banners(
    std::function<void(std::vector<std::uint8_t>&, int, int)> fn) {
    ctx_.draw_banners = std::move(fn);
}

// Vector HUD text over the centre 320 sub-region of a wide output: map the
// native HUD x into the wide domain (x + margin) then scale to output, with
// the glyph cap sized against the WIDE logical width so glyphs are not
// oversized.  Moved verbatim from run_platform_level (CC2d).
void WidescreenPresenter::draw_wide_hud_text(
    std::vector<std::uint8_t>& b, int ow, int oh,
    const enhance::EnhancedHudLayout& L) {
    const int cap = 8 * ow / native_w_;
    ctx_.hd_text->set_cap_px(cap > 0 ? cap : 1);
    const double sx = static_cast<double>(ow) / native_w_;
    const double sy = oh / 200.0;
    for (const auto& t : L.texts) {
        const int x = static_cast<int>((t.x + margin_) * sx + 0.5);
        const int y = static_cast<int>(t.baseline_y * sy + 0.5);
        ctx_.hd_text->draw(b, ow, oh, x, y, t.str, t.r, t.g, t.b);
    }
    // Widescreen banner substitutes (covers every present() path + the
    // pillarboxed-WS upload_and_show branch, which all route here).
    if (ctx_.draw_banners) ctx_.draw_banners(b, ow, oh);
}

void WidescreenPresenter::set_float_pos(bool use, float fx, float fy) {
    use_float_pos_ = use;
    player_fx_ = fx;
    player_fy_ = fy;
}

// Recompute widescreen state when the renderer output size changes (Alt+Enter
// fullscreen toggle / window resize).  Cheap no-op when unchanged.  Mirrors
// boss_app.cpp's rebuild_ws_if_resized: recomputes the margin (and so
// active_ / native_w_), recreates the wide texture, AND updates
// logical_w/logical_h in lockstep with SDL's logical size — the text-overlay
// flush restores the SDL logical size from those vars, so updating only SDL's
// logical size gets clobbered back to the stale dims on the next HUD draw,
// squashing the wide buffer into the old canvas (the fullscreen bug).  Scoped
// to widescreen (opts.aspect tracks Tier-1 live changes); other aspects keep
// SDL's aspect-preserving letterbox, which already toggles correctly.
// refresh_level_state(): rebuilds the LEVEL-DERIVED widescreen state (FOND
// backdrop + neighbour peek cache) when widescreen turns ON mid-level.  Those
// are built ONCE at level entry gated on active_; if the level started
// WINDOWED (inactive) they were skipped, so a later Alt+Enter would activate
// the wide present with an EMPTY neighbour cache (present_path() false →
// upload_and_show pillarbox = black bars until the next transition) and a
// NULL backdrop (no-neighbour margins self-tile = corrupted screen-0 left
// edge).
void WidescreenPresenter::rebuild_if_resized() {
    if (*ctx_.aspect != "widescreen" || !ctx_.hd) return;
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(ctx_.ren, &ow, &oh);
    if (ow == ow0_ && oh == oh0_) return;   // unchanged
    ow0_ = ow;
    oh0_ = oh;
    const bool was_active = active_;
    const int newM = compute_margin(ow, oh);
    if (newM != margin_) {
        margin_ = newM;
        native_w_ = 320 + 2 * margin_;
        if (wtex_ != nullptr) { SDL_DestroyTexture(wtex_); wtex_ = nullptr; }
        if (margin_ > 0)
            wtex_ = SDL_CreateTexture(ctx_.ren, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      native_w_ * ctx_.hd_scale,
                                      200 * ctx_.hd_scale);
    }
    active_ = (*ctx_.aspect == "widescreen") && ctx_.hd && margin_ > 0;
    // Active → wide canvas fills the output; inactive (margin 0 → 16:10
    // display) → the aspect_logical fallback.  Keep SDL's logical size and
    // the overlay-restore vars in lockstep.
    *ctx_.logical_w =
        active_ ? native_w_ * ctx_.hd_scale : ctx_.fallback_ld.w;
    *ctx_.logical_h = active_ ? 200 * ctx_.hd_scale : ctx_.fallback_ld.h;
    SDL_RenderSetLogicalSize(ctx_.ren, *ctx_.logical_w, *ctx_.logical_h);
    // Turned ON mid-level: build the level-derived state the inactive level
    // entry skipped, so the very next present this frame has a live cache +
    // backdrop (no black-bar / corruption window).
    if (active_ && !was_active) refresh_level_state();
}

// update_cache (re)composes the peek neighbours for the current screen;
// build_backdrop rebuilds the FOND — the mid-level activation refresh.
void WidescreenPresenter::refresh_level_state() {
    build_backdrop();
    update_cache();
}

// ── Widescreen neighbor cache (§8.7) ───────────────────────────────────
// STATIC per-screen (bg + terrain, no entities, no RNG), so composed once
// on screen-bind and reused every frame.  update_cache resolves the coherent
// horizontal neighbors for the current screen and composes each into a
// native 320×200 FrameBuffer via the read-only compose_static callback
// (compose_surface_screen_static — never perturbs g.state / RNG).
void WidescreenPresenter::update_cache() {
    ++peek_generation_;
    left_ok_ = right_ok_ = false;
    left_screen_ = right_screen_ = -1;
    left_seam_.clear();
    right_seam_.clear();
    left_bridge_.clear();
    right_bridge_.clear();
    left_mons_.clear();
    right_mons_.clear();
    if (!active_) return;
    const int surface_count = ctx_.surface_screen_count;
    const auto nb = presentation::widescreen_neighbors(
        ctx_.internal_level_id, ctx_.state->current_screen,
        ctx_.state->secret_flag != 0, surface_count);
    presentation::LevelRenderAssets nra;   // neighbour assets, built once
    // The CURRENT screen's straddling tiles reach into the neighbours'
    // space; complete them INSIDE the peeks as underlay (over backdrop,
    // under the neighbour's authored tiles) — the earlier margins-only
    // re-blit painted them OVER the finished peek and buried the
    // neighbour's dirt-top rows.
    std::vector<presentation::LevelRenderAssets::TileDraw> cur_l, cur_r;
    for (const auto& t : presentation::tile_patterns::seam_straddling_tiles(
             ctx_.render->tiles, ctx_.render->tile_sprites,
             /*right_edge=*/false))
        cur_l.push_back({t.sprite_idx, t.x + 320, t.y});
    for (const auto& t : presentation::tile_patterns::seam_straddling_tiles(
             ctx_.render->tiles, ctx_.render->tile_sprites,
             /*right_edge=*/true))
        cur_r.push_back({t.sprite_idx, t.x - 320, t.y});
    if (nb.left >= 0) {
        ctx_.compose_static(nb.left, left_, &nra, &cur_l,
                            /*frozen_full=*/false,
                            /*peek_monsters=*/false);
        left_mons_ = ctx_.collect_monsters(nb.left);
        left_ok_ = true;
        left_screen_ = nb.left;
        left_seam_ = presentation::tile_patterns::seam_straddling_tiles(
            nra.tiles, nra.tile_sprites, /*right_edge=*/true);
        // Authored seam holes: bridge a contiguous decor row broken at
        // the seam (left neighbour = A, current = B).  Bridge tiles come
        // back in A coordinates — exactly what the left seam list blits
        // at -320 — and ride the same layering laws as the straddlers.
        for (const auto& b : presentation::tile_patterns::seam_row_bridges(
                 nra.tiles, nra.backdrop_tile_count, ctx_.render->tiles,
                 ctx_.render->backdrop_tile_count, nra.tile_sprites))
            left_bridge_.push_back(b);
    }
    if (nb.right >= 0) {
        ctx_.compose_static(nb.right, right_, &nra, &cur_r,
                            /*frozen_full=*/false,
                            /*peek_monsters=*/false);
        right_mons_ = ctx_.collect_monsters(nb.right);
        right_ok_ = true;
        right_screen_ = nb.right;
        right_seam_ = presentation::tile_patterns::seam_straddling_tiles(
            nra.tiles, nra.tile_sprites, /*right_edge=*/false);
        // Bridges with current = A, right neighbour = B: returned in
        // CURRENT-screen coords; the right seam list is in NEIGHBOUR
        // coords (blitted at +320), so shift by -320.
        for (const auto& b : presentation::tile_patterns::seam_row_bridges(
                 ctx_.render->tiles, ctx_.render->backdrop_tile_count,
                 nra.tiles, nra.backdrop_tile_count,
                 ctx_.render->tile_sprites))
            right_bridge_.push_back({b.sprite_idx, b.x - 320, b.y});
    }
}

// Pure-FOND backdrop (320×200 RGBA) for the no-neighbour margin extension:
// the SAME HUD-erased FOND the center uses (g.render.background, rows 0..8
// already flattened to bgp[0] at level entry for surface levels), converted
// to RGBA by the same palette path as draw_bg_base.  Contains
// sky+mountains+clouds but NO foreground tiles (the palm is a tile drawn over
// this), so sampling its edge column extends the backdrop without duplicating
// foreground.  Built once per level (background is level-stable).  Surface
// (visual) levels only; secret rooms (no visual_background → colour fill) get
// a NULL backdrop so compose_widescreen keeps the legacy self-tile fill
// unchanged.  Also callable from the Alt+Enter mid-level activation path
// (refresh_level_state) — at inactive level entry the gate below is false and
// it would otherwise never exist for that level.
void WidescreenPresenter::build_backdrop() {
    backdrop_ok_ = false;
    if (active_ && ctx_.render->visual_background &&
        ctx_.render->background.width == 320 &&
        ctx_.render->background.pixels.size() >= 320u * 200u) {
        const auto& bg = ctx_.render->background;
        for (std::size_t i = 0; i < 320u * 200u; ++i) {
            const std::uint8_t idx = bg.pixels[i];
            const formats::Rgb c = (idx < bg.palette.size())
                                       ? bg.palette[idx] : formats::Rgb{};
            backdrop_.px[i * 4] = c.r; backdrop_.px[i * 4 + 1] = c.g;
            backdrop_.px[i * 4 + 2] = c.b; backdrop_.px[i * 4 + 3] = 255;
        }
        backdrop_ok_ = true;
    }
}

// Secret rooms (secret_flag set AND current_screen >= 100) get the
// widescreen present too, but with BOTH neighbors null so compose_widescreen
// edge-clamps both margins to the room's own background (self-tile, §8.7).
// widescreen_neighbors returns {-1,-1} here so left_ok_/right_ok_ are
// already false — no neighbor compose needed.  Caves/bosses stay pillarbox.
bool WidescreenPresenter::secret_selftile() const {
    return active_ && ctx_.state->secret_flag != 0 &&
           ctx_.state->current_screen >= 100;
}

// ONE authoritative present-path predicate: widescreen runs (peek OR
// secret self-tile).  Selects present() (composes its own wide
// buffer with the entity-overflow pass) over upload_and_show, so secret
// rooms get the same overflow + single-advance guarantee as the neighbour-
// peek path.  This SAME predicate gates every present call site, so the
// "is the overflow pass running this frame?" decision cannot diverge between
// sites — and the main fb compose always advances club_flag exactly once.
bool WidescreenPresenter::present_path() const {
    return active_ && (left_ok_ || right_ok_ || secret_selftile());
}

// Tier-1 living margins: cycle the peek monsters' walk sprites IN
// PLACE, once per logic tick.  No translation, no RNG, no collision —
// pure sprite animation on cloned lists (the anchor at the spawn post
// is what keeps entry pop-free).  L3A alternates ride the live global
// phase counter so margin cadence matches the centre screen.
void WidescreenPresenter::tick_margin_monsters() {
    auto tick_margin = [&](std::vector<core::Entity>& v,
                           bool face_left) {
        for (auto& m : v) {
            ++m.state_counter;
            const bool use_alt =
                m.alt_spr_num >= 0 &&
                ctx_.state->l3a_phase_counter <= 16;
            const int base = use_alt ? m.alt_spr_num : m.spr_num;
            const auto& offs =
                use_alt ? m.alt_walk_offsets : m.walk_offsets;
            m.sprite =
                offs.empty()
                    ? base
                    : base + offs[static_cast<std::size_t>(
                                m.state_counter) %
                            offs.size()];
            m.facing_left = face_left;
        }
    };
    // Face toward the player's screen.
    tick_margin(left_mons_, /*face_left=*/false);
    tick_margin(right_mons_, /*face_left=*/true);
}

// Tier-1 living margins: draw the animated spawn-post monsters over the
// margin regions of a WIDE target (origin_x must already be margin_).
// The lists live in NEIGHBOUR-screen coordinates; shift left by -320 and
// right by +320, and hard-clip each side to its own margin so a monster
// near the seam never draws over the live centre (the centre is the real
// screen's territory — its own entities draw there).
void WidescreenPresenter::draw_margin_monsters(RenderTarget& wrt) {
    const auto saved_lo = wrt.clip_x_lo;
    const auto saved_hi = wrt.clip_x_hi;
    auto draw_side = [&](const std::vector<core::Entity>& v, int shift,
                         int lo, int hi) {
        wrt.clip_x_lo = lo;
        wrt.clip_x_hi = hi;
        for (const auto& m : v) {
            if (m.sprite < 0 ||
                m.sprite >=
                    static_cast<int>(ctx_.render->entity_sprites.size()))
                continue;
            presentation::blit_sprite(
                wrt,
                ctx_.render->entity_sprites[static_cast<std::size_t>(
                    m.sprite)],
                ctx_.render->palette, m.x + shift, m.y, m.facing_left);
        }
    };
    if (left_ok_)
        draw_side(left_mons_, -320, 0, margin_ * wrt.scale);
    if (right_ok_)
        draw_side(right_mons_, +320, (margin_ + 320) * wrt.scale,
                  1 << 28);
    wrt.clip_x_lo = saved_lo;
    wrt.clip_x_hi = saved_hi;
}

// ── Widescreen present (§8.7, Option A) ────────────────────────────────
// Composes its OWN native 320×200 center BACKGROUND (bg + tiles only, no
// entities), assembles the wide native buffer (that center + cached neighbour
// terrain margins), then draws the live foreground ONCE over the WIDE buffer
// at origin_x = margin_ so entities crossing the 320 edge OVERFLOW into the
// margins (the kept widescreen effect).  Upscales the WHOLE wide buffer,
// uploads to the wide texture, presents.  The HUD bars are baked into the
// native center; the vector HUD text is drawn into the output overlay mapped
// to the center 320 sub-region of the wide canvas.
//
// CLUB-MECHANIC FIX: this overflow draw_entities uses advance_state = FALSE,
// so it draws the SAME club_flag the main fb compose already advanced but
// does NOT mutate gameplay state again.  The single authoritative advance is
// the main fb compose (compose_frame on `fb`, advance_state true), which runs
// once per gameplay tick BEFORE the smooth-motion sub-frame save/restore — so
// it survives.  Two entity DRAWS per frame (the unshown fb + this overflow),
// but exactly ONE state advance.  This is why fb is never reused as the
// center here: its entities would be hard-clipped at 320; the overflow draw
// is the one shown.
//
// KNOWN v1 TRADEOFF: the center goes through the whole-frame upscaler here
// rather than the per-asset-HD compose path (per-asset-HD center is a
// follow-up).  Used only when active_ AND not mid-transition AND
// at least one neighbor is present; otherwise the caller uses upload_and_show.
void WidescreenPresenter::present(
    const std::function<void(RenderTarget&)>& bubble_hook_w,
    bool do_present) {
    rebuild_if_resized();   // Alt+Enter / resize: recompute wide state
    // ── FAST PATH (task #61): cached HD static wide bg + HD sprites ──
    // The wide static background (centre bg+tiles + peek margins) is
    // STATIC within a flip-screen screen, so cache its upscaled HD form
    // per screen (get_static_wide_bg_hd) and each frame just memcpy it +
    // draw the dynamic sprites/HUD bars at HD via the per-asset cache —
    // exactly the non-widescreen pipeline.  Avoids the per-frame whole-
    // frame upscale (omniscale 36ms→~3ms).  Secret rooms (a post-bg hook
    // draws the animated fluid bubbles) ALSO use this path: the bubbles
    // draw at HD over the cached bg, then redraw_bg_tiles puts the floor
    // back on top so the bubbles stay BEHIND it (pixel-identical to the
    // slow base→bubbles→tiles order).  HD only (scale>1).
    if (ctx_.hd && ctx_.hd_scale > 1 && wtex_ != nullptr) {
        const FrameBuffer* ws_bd =
            (backdrop_ok_ && ctx_.render->visual_background) ? &backdrop_
                                                             : nullptr;
        const std::vector<std::uint8_t>& bg_hd =
            presentation::get_static_wide_bg_hd(
                *ctx_.state, *ctx_.render, ctx_.hd_scale, *ctx_.hd_profile,
                margin_,
                left_ok_ ? &left_ : nullptr, left_screen_,
                right_ok_ ? &right_ : nullptr, right_screen_,
                ws_bd, left_seam_, right_seam_, left_bridge_,
                right_bridge_, peek_generation_);
        const int uw = native_w_ * ctx_.hd_scale, uh = 200 * ctx_.hd_scale;
        const std::size_t n =
            static_cast<std::size_t>(uw) * uh * 4;
        if (frame_hd_.size() != n) frame_hd_.resize(n);
        std::memcpy(frame_hd_.data(), bg_hd.data(),
                    std::min(n, bg_hd.size()));
        // Dynamic foreground at HD (per-asset cache), origin_x = margin
        // so edge-crossing sprites overflow into the margins; advance_
        // state=false (visual only — the authoritative advance already
        // ran on `fb`).
        {
            RenderTarget wrt{frame_hd_.data(), uw, uh, ctx_.hd_scale,
                             ctx_.hd_cache, ctx_.hd_profile};
            wrt.origin_x = margin_;
            wrt.advance_state = false;
            wrt.use_float_pos = use_float_pos_;
            wrt.player_fx = player_fx_;
            wrt.player_fy = player_fy_;
            // Secret room: the post-bg hook draws the fluid bubbles over
            // the cached bg; redraw the floor tiles on top so the bubbles
            // stay behind them (matches draw_background's base→bubbles→
            // tiles order).  Both unclipped (background), before the
            // foreground floor-clip is set for entities/player.
            if (bubble_hook_w) {
                bubble_hook_w(wrt);
                presentation::redraw_bg_tiles(wrt, *ctx_.state, *ctx_.render);
            }
            if (ctx_.state->secret_flag)
                wrt.clip_y = (168 + 1) * ctx_.hd_scale;
            // Clip the live entity/player overflow at a NO-NEIGHBOUR
            // margin: a level edge has no real screen beyond x=320, so
            // the DOS hard clip applies — the player must not spill onto
            // the synthetic edge-fill (backdrop/ground extension or the
            // mirror), which read as a "mirrored player" at the L3
            // trunk exit.  Where a real neighbour peek exists, keep the
            // overflow (the smooth screen-to-screen enhancement).
            // Icy level (internal 5) glider FLY-AWAY off the last screen:
            // the glider+player exit to the RIGHT, so let them overflow
            // into the no-neighbour right margin (fly off naturally)
            // instead of being clipped at x=320 — the "glider cut off at
            // the widescreen edge".  Mirrors the L4 ride-off overflow.
            const bool glider_flyoff =
                ctx_.state->current_level == 5 && ctx_.state->glider_active &&
                ctx_.state->current_screen == core::kLastScreen;
            // PLAYER-only clip: flying entities (the bird) overflow into
            // the margin naturally; only the player is held back from the
            // synthetic edge-fill (the "mirrored player").  EXCEPT secret
            // rooms, whose margins self-tile the room's OWN walls — a real
            // continuation of the enclosed room, not a level-edge mirror — so
            // the player overflows into them instead of being sliced at the
            // old 320 edge (the "player cut off at the widescreen strip").
            const bool secret_overflow = ctx_.state->secret_flag != 0;
            if (!left_ok_ && !secret_overflow)
                wrt.player_clip_x_lo = margin_ * ctx_.hd_scale;
            if (!right_ok_ && !glider_flyoff && !secret_overflow)
                wrt.player_clip_x_hi = (margin_ + 320) * ctx_.hd_scale;
            presentation::draw_entities(wrt, *ctx_.state, *ctx_.render,
                                        /*draw_player=*/true);
            // Lava bubbles are intentionally reflected INTO the margin
            // (L7) — un-clip before that pass so the no-neighbour clip
            // above does not erase them.
            wrt.clip_x_lo = -(1 << 28);
            wrt.clip_x_hi = 1 << 28;
            // Reflect L7 lava bubbles onto the mirrored lava in the
            // no-neighbour margin (no-op on other levels).
            presentation::draw_mirrored_lava_bubbles(
                wrt, *ctx_.state, *ctx_.render, /*mirror_left=*/!left_ok_,
                /*mirror_right=*/!right_ok_);
            if (ctx_.draw_overlay_tail) ctx_.draw_overlay_tail(wrt);
            draw_margin_monsters(wrt);   // Tier-1 living margins
        }
        // HUD bars at HD, shifted to the centre (x_off = margin_).
        enhance::EnhancedHudLayout hud_layout;
        const bool draw_hud_overlay = ctx_.use_hd_text;
        if (draw_hud_overlay) {
            hud_layout =
                enhance::compute_enhanced_hud_layout(*ctx_.hd_text,
                                                     *ctx_.state);
            enhance::draw_enhanced_hud_bars(frame_hd_, uw, uh, ctx_.hd_scale,
                                            hud_layout, margin_);
        }
        dump_steady_wide(frame_hd_.data(), uw, uh);
        SDL_UpdateTexture(wtex_, nullptr, frame_hd_.data(), uw * 4);
        SDL_RenderClear(ctx_.ren);
        SDL_RenderCopy(ctx_.ren, wtex_, nullptr, nullptr);
        if (draw_hud_overlay && ctx_.hd_text->ok()) {
            int ow = 0, oh = 0;
            if (ctx_.text_overlay->begin(ctx_.ren, *ctx_.hd_text, ow, oh)) {
                draw_wide_hud_text(ctx_.text_overlay->buffer(), ow, oh,
                                        hud_layout);
                ctx_.text_overlay->flush(ctx_.ren, *ctx_.logical_w,
                                         *ctx_.logical_h);
            }
        }
        if (do_present) SDL_RenderPresent(ctx_.ren);
        return;
    }
    // ── SLOW PATH (whole-frame upscale): secret rooms w/ bubbles, etc. ──
    // Native center: BACKGROUND + tiles ONLY (no entities yet).  The
    // foreground is drawn ONCE later, over the assembled WIDE buffer, so
    // live entities crossing the 320 edge overflow into the margins
    // instead of being hard-clipped.  Composing the center entity-free
    // here means this pass performs NO state mutation (the overflow
    // draw_entities below is advance_state = false too).
    FrameBuffer center{};   // 320×200
    {
        RenderTarget rt{center.px.data(), 320, 200, 1, nullptr, nullptr};
        presentation::draw_background(rt, *ctx_.state, *ctx_.render,
                                      bubble_hook_w);
    }
    // HUD: the state-mutating draw (food cap / GET READY decrement)
    // already ran ONCE on `fb` via draw_hud_for_fb in the frame loop, so
    // here we only READ the layout (compute is pure) and bake the
    // non-text bars into the native center at scale 1.  No second
    // draw_hud_for_fb — that would double-decrement GET READY.
    enhance::EnhancedHudLayout hud_layout;
    const bool draw_hud_overlay = ctx_.use_hd_text;
    if (draw_hud_overlay) {
        hud_layout = enhance::compute_enhanced_hud_layout(*ctx_.hd_text,
                                                          *ctx_.state);
        enhance::draw_enhanced_hud_bars(center.px, 320, 200, 1, hud_layout);
    }
    // Assemble wide native: center (bg+tiles+HUD bars) verbatim into the
    // middle, neighbour terrain into the margins (no entities anywhere).
    std::vector<std::uint8_t> wide;
    // Backdrop is passed ONLY for a screen that currently HAS a visual
    // FOND backdrop.  backdrop_ is built once at level entry from the
    // surface FOND, so backdrop_ok_ stays true after entering the
    // secret room — but the secret room has no FOND (g.render.visual_
    // background == false there).  Gating on the CURRENT screen's
    // visual_background stops the stale surface mountains bleeding into
    // the secret-room margins; a null backdrop falls back to the
    // self-tile fill the enclosed room wants.
    const FrameBuffer* ws_bd =
        (backdrop_ok_ && ctx_.render->visual_background) ? &backdrop_
                                                         : nullptr;
    presentation::compose_widescreen(
        wide, margin_, center,
        left_ok_ ? &left_ : nullptr,
        right_ok_ ? &right_ : nullptr, /*hud_rows=*/0, ws_bd,
        /*reflect_pure=*/false, /*margin_edge_brightness=*/1.0f,
        /*repeat_no_backdrop=*/ctx_.state->secret_flag == 0);
    // Foreground over the WIDE native buffer at origin_x = margin: the
    // live-entity + player overflow pass.  In-center pixels land exactly
    // where compose_widescreen placed the center; any sprite crossing the
    // 320 edge spills into the neighbour-terrain margin (enhanced-only
    // divergence from the DOS hard clip at 320).  advance_state = false:
    // purely visual, NO double-advance (see the block comment above).
    {
        RenderTarget wrt{wide.data(), native_w_, 200,
                         1, nullptr, nullptr};
        wrt.origin_x = margin_;
        wrt.advance_state = false;
        wrt.use_float_pos = use_float_pos_;
        wrt.player_fx = player_fx_;
        wrt.player_fy = player_fy_;
        // Secret-room foreground floor-clip.  The non-widescreen path
        // (compose_frame) gets this clip because draw_background sets
        // t.clip_y on the SAME target draw_entities then reads.  Here
        // draw_background ran on the center `rt` (a different target),
        // so the wide entity pass would lose it and the trampoline
        // springs would poke over the floor again.  Mirror the same
        // height clamp on `wrt` (scale 1, width-independent — it is a
        // Y bound).  Keep this in lockstep with draw_background's secret
        // clip: FUN_1052_2813 / exe_vga_path_screen_height_clip_omission.
        if (ctx_.state->secret_flag) wrt.clip_y = (168 + 1) * wrt.scale;
        // No-neighbour margin clip — PLAYER only, so flying entities (the
        // bird) overflow naturally into the margin while the player can't
        // spill onto the synthetic edge-fill.  Scale 1 here (native).  Secret
        // rooms are exempt: their self-tiled margins continue the enclosed
        // room, so the player overflows into them (no cut-off at x=320).
        const bool secret_overflow = ctx_.state->secret_flag != 0;
        if (!left_ok_ && !secret_overflow) wrt.player_clip_x_lo = margin_;
        if (!right_ok_ && !secret_overflow) wrt.player_clip_x_hi = margin_ + 320;
        presentation::draw_entities(wrt, *ctx_.state, *ctx_.render,
                                    /*draw_player=*/true);
        wrt.clip_x_lo = -(1 << 28);
        wrt.clip_x_hi = 1 << 28;
        presentation::draw_mirrored_lava_bubbles(
            wrt, *ctx_.state, *ctx_.render, /*mirror_left=*/!left_ok_,
            /*mirror_right=*/!right_ok_);
        if (ctx_.draw_overlay_tail) ctx_.draw_overlay_tail(wrt);
        draw_margin_monsters(wrt);   // Tier-1 living margins
    }
    dump_steady_wide(wide.data(), native_w_, 200);
    // Upscale the whole wide buffer to (native_w_*hd_scale × 200*hd_scale).
    std::vector<std::uint8_t> up =
        enhance::upscale_rgba(wide, native_w_, 200, ctx_.hd_scale,
                              *ctx_.hd_profile);
    SDL_UpdateTexture(wtex_, nullptr, up.data(),
                      native_w_ * ctx_.hd_scale * 4);
    SDL_RenderClear(ctx_.ren);
    SDL_RenderCopy(ctx_.ren, wtex_, nullptr, nullptr);
    // Vector HUD text over the center 320 sub-region.  Map native x into
    // the wide domain ([margin, margin+320]) then to output by
    // ow/native_w; set the font cap to the wide-domain output scale so
    // glyphs are not oversized.  (Pause/cheat menus over widescreen are a
    // follow-up — they fall through to the standard center-only path.)
    if (draw_hud_overlay && ctx_.hd_text->ok()) {
        int ow = 0, oh = 0;
        if (ctx_.text_overlay->begin(ctx_.ren, *ctx_.hd_text, ow, oh)) {
            draw_wide_hud_text(ctx_.text_overlay->buffer(), ow, oh,
                                    hud_layout);
            ctx_.text_overlay->flush(ctx_.ren, *ctx_.logical_w,
                                     *ctx_.logical_h);
        }
    }
    // do_present=false leaves the wide frame in the backbuffer for a
    // caller-side RenderReadPixels (Metal reads black AFTER present).
    if (do_present) SDL_RenderPresent(ctx_.ren);
}

// ── Widescreen transition present (§8.7 wide transitions) ───────────────
// Present a ready-made WIDE native buffer (native_w() × 200, NO baked HUD)
// through the wide texture, exactly like present()'s tail: upscale
// whole-frame → wtex_ → RenderCopy, then draw the FIXED HUD over the centre
// 320 sub-region (bars into the upscaled output + vector text overlay).  Used
// for every frame of a widescreen screen-change transition so width AND HUD
// position stay continuous with the steady widescreen frame — no 320
// pillarbox pop, no HUD jump.  The sim is PAUSED during transitions, so the
// HUD layout is computed pure-read from the state (no state-advance here; the
// single authoritative draw_hud_for_fb already ran before play_transition).
// Moved verbatim from run_platform_level (CC2d).
void WidescreenPresenter::present_transition(std::vector<std::uint8_t>& wide,
                                             bool with_hud,
                                             bool pre_upscaled) {
    // NB: do NOT rebuild_if_resized() here.  `wide` is a pre-built buffer
    // sized at the caller's native_w() (transitions build oldw/neww/work ONCE
    // and reuse them across the animation loop); changing native_w() mid-call
    // would make upscale_rgba misread it.  Transitions are sub-second and
    // self-contained — an Alt+Enter pressed mid-transition is absorbed by the
    // next steady-state present (upload_and_show / present()).
    // pre_upscaled: `wide` is ALREADY the HD buffer (native_w()*hd_scale ×
    // 200*hd_scale) — skip the per-frame upscale.  The panorama pan upscales
    // its static strip ONCE and windows it, so each frame just presents the
    // pre-upscaled window (no per-frame omniscale → smooth; mirrors the
    // steady-state static-bg cache, task #61).
    std::vector<std::uint8_t> up;
    if (!pre_upscaled)
        up = enhance::upscale_rgba(wide, native_w_, 200, ctx_.hd_scale,
                                   *ctx_.hd_profile);
    std::vector<std::uint8_t>& hdbuf = pre_upscaled ? wide : up;
    // Fixed HUD bars over the centre 320 region (== steady-frame position):
    // present() bakes the bars into the static centre; in a
    // transition the centre slides, so instead draw the bars into the
    // upscaled output at the centre offset (margin_*hd_scale) where they
    // stay put across the whole pan/fade — matching the steady frame.
    // with_hud=false suppresses the HUD entirely (the level-end fade-to-black
    // darkens the HUD with the scene, matching the classic with_hud=false fade).
    const bool draw_hud_overlay = with_hud && ctx_.hd && ctx_.use_hd_text;
    enhance::EnhancedHudLayout hud_layout;
    if (draw_hud_overlay) {
        hud_layout =
            enhance::compute_enhanced_hud_layout(*ctx_.hd_text, *ctx_.state);
        // draw_enhanced_hud_bars draws relative to a 320*scale-wide buffer;
        // wrap a 320-wide view by offsetting into the wide row.  Simplest:
        // composite a centre 320*scale strip, draw bars there, copy back.
        const int cw = 320 * ctx_.hd_scale;   // centre width in output px
        const int ch = 200 * ctx_.hd_scale;
        const int cx = margin_ * ctx_.hd_scale; // centre x-origin in wide output
        std::vector<std::uint8_t> centre(
            static_cast<std::size_t>(cw) * ch * 4);
        for (int y = 0; y < ch; ++y)
            std::copy_n(hdbuf.begin() +
                            (static_cast<std::size_t>(y) * native_w_ *
                                 ctx_.hd_scale + cx) * 4,
                        static_cast<std::size_t>(cw) * 4,
                        centre.begin() +
                            static_cast<std::size_t>(y) * cw * 4);
        enhance::draw_enhanced_hud_bars(centre, cw, ch, ctx_.hd_scale,
                                        hud_layout);
        for (int y = 0; y < ch; ++y)
            std::copy_n(centre.begin() +
                            static_cast<std::size_t>(y) * cw * 4,
                        static_cast<std::size_t>(cw) * 4,
                        hdbuf.begin() +
                            (static_cast<std::size_t>(y) * native_w_ *
                                 ctx_.hd_scale + cx) * 4);
    }
    // Test hook: OLDUVAI_WIDE_TRANSITION_DUMP=<dir> saves the wide HD
    // buffer AFTER the HUD-bar splice, right before texture upload — the
    // CPU-side output of this present (no renderer readback, no vector
    // text, which is float-rasterized and not hash-stable).  Hashed by
    // the wide_transition ctest; determinism requires an INTEGER
    // hd_profile (mmpx/xbr) — see tests/wide_transition.sh.
    if (const char* wd = std::getenv("OLDUVAI_WIDE_TRANSITION_DUMP")) {
        static int wtseq = 0;
        char wtpath[512];
        std::snprintf(wtpath, sizeof wtpath, "%s/wpresent_%04d.png", wd,
                      wtseq++);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
            hdbuf.data(), native_w_ * ctx_.hd_scale, 200 * ctx_.hd_scale, 32,
            native_w_ * ctx_.hd_scale * 4, SDL_PIXELFORMAT_RGBA32);
        if (s) { save_surface_image(s, wtpath); SDL_FreeSurface(s); }
    }
    SDL_UpdateTexture(wtex_, nullptr, hdbuf.data(),
                      native_w_ * ctx_.hd_scale * 4);
    SDL_RenderClear(ctx_.ren);
    SDL_RenderCopy(ctx_.ren, wtex_, nullptr, nullptr);
    // Vector HUD text over the centre 320 sub-region (same mapping as
    // present()): map native x into the wide domain then to output.
    if (draw_hud_overlay && ctx_.hd_text->ok()) {
        int ow = 0, oh = 0;
        if (ctx_.text_overlay->begin(ctx_.ren, *ctx_.hd_text, ow, oh)) {
            draw_wide_hud_text(ctx_.text_overlay->buffer(), ow, oh,
                                    hud_layout);
            ctx_.text_overlay->flush(ctx_.ren, *ctx_.logical_w,
                                     *ctx_.logical_h);
        }
    }
    SDL_RenderPresent(ctx_.ren);
}

// Wrap a native 320×200 center (bg+tiles+player, NO baked HUD) into a WIDE
// native buffer using the CURRENT widescreen cache (left_/right_/backdrop_)
// — the caller sequences the cache so the outgoing frame is wrapped BEFORE
// the rebind (old neighbours) and the incoming frame AFTER (new neighbours).
// Same margin rules as present(): peek where a neighbour exists,
// backdrop/self-tile fill otherwise.  hud_rows=0 because the center carries
// no baked HUD here (the HUD is drawn fixed by present_wide_transition).
// No state mutation, no RNG.
void WidescreenPresenter::wrap_wide(const FrameBuffer& center,
                                    std::vector<std::uint8_t>& out) {
    const FrameBuffer* ws_bd =
        (backdrop_ok_ && ctx_.render->visual_background) ? &backdrop_
                                                         : nullptr;
    presentation::compose_widescreen(
        out, margin_, center,
        left_ok_ ? &left_ : nullptr,
        right_ok_ ? &right_ : nullptr, /*hud_rows=*/0, ws_bd,
        /*reflect_pure=*/false, /*margin_edge_brightness=*/1.0f,
        /*repeat_no_backdrop=*/ctx_.state->secret_flag == 0);
}

// Wrap a native 320 center into a WIDE buffer with PURE-BLACK bezel margins
// (same fill upload_and_show uses for a pillarboxed widescreen frame).  Used
// for a transition side that is NOT a present_path screen (e.g. a cave) but
// is part of a widescreen transition — so its wide frame matches its own
// steady pillarbox look at the wide width, keeping the centre 320 (and the
// HUD over it) at exactly the same position as the peek side.
void WidescreenPresenter::wrap_wide_bezel(const FrameBuffer& center,
                                          std::vector<std::uint8_t>& out) {
    // Pure black (0,0,0): a dark-gray bezel read as grayish next to a cave's
    // true-black tiles; black matches and is the conventional letterbox.
    out.assign(static_cast<std::size_t>(native_w_) * 200 * 4, 0);
    for (std::size_t i = 3; i < out.size(); i += 4) out[i] = 255;  // alpha
    for (int y = 0; y < 200; ++y)
        std::copy_n(center.px.begin() + static_cast<std::size_t>(y) * 320 * 4,
                    static_cast<std::size_t>(320) * 4,
                    out.begin() + (static_cast<std::size_t>(y) * native_w_ +
                                   margin_) * 4);
}

// Choose peek-vs-bezel wide wrap for a side by its present_path status.
void WidescreenPresenter::wrap_wide_for(const FrameBuffer& center,
                                        bool is_present,
                                        std::vector<std::uint8_t>& out) {
    if (is_present) wrap_wide(center, out);
    else            wrap_wide_bezel(center, out);
}

// Re-apply the neighbour seam content (straddler completions + row
// bridges) that a raw 320-centre memcpy just buried: the seam lists'
// tiles may cross INTO the centre (a bush overhang, the S1|S2 rail
// bridge at x=304..320), and any buffer assembled as "static wide bg,
// then centre overwritten" loses them.  Band-limited to the tiles'
// actual extents, then the screen's own authored level tiles go back
// on top — the same layering laws as the pan strip / descent presenter.
void WidescreenPresenter::reapply_seam_bands(std::vector<std::uint8_t>& wide) {
    if (left_seam_.empty() && right_seam_.empty() &&
        left_bridge_.empty() && right_bridge_.empty())
        return;
    presentation::RenderTarget drt{wide.data(), native_w_, 200, 1,
                                   nullptr, nullptr};
    drt.origin_x = margin_;
    std::vector<std::pair<int, int>> bands;
    auto overhang =
        [&](const std::vector<presentation::LevelRenderAssets::TileDraw>&
                seam,
            bool from_left) {
            for (const auto& tp : seam) {
                if (tp.sprite_idx < 0 ||
                    tp.sprite_idx >=
                        static_cast<int>(ctx_.render->tile_sprites.size()))
                    continue;
                const auto& spr = ctx_.render->tile_sprites
                    [static_cast<std::size_t>(tp.sprite_idx)];
                const int b_lo =
                    from_left ? margin_ : margin_ + 320 + tp.x;
                const int b_hi = from_left
                                     ? margin_ + tp.x + spr.width - 320
                                     : margin_ + 320;
                if (b_hi <= b_lo) continue;
                drt.clip_x_lo = b_lo;
                drt.clip_x_hi = b_hi;
                presentation::blit_sprite(drt, spr, ctx_.render->palette,
                                          tp.x + (from_left ? -320 : +320),
                                          tp.y);
                bands.emplace_back(b_lo, b_hi);
            }
        };
    overhang(left_seam_, /*from_left=*/true);
    overhang(right_seam_, /*from_left=*/false);
    overhang(left_bridge_, /*from_left=*/true);
    overhang(right_bridge_, /*from_left=*/false);
    for (const auto& [blo, bhi] : bands) {
        drt.clip_x_lo = std::max(blo, margin_);
        drt.clip_x_hi = std::min(bhi, margin_ + 320);
        if (drt.clip_x_lo >= drt.clip_x_hi) continue;
        const int n0 = std::max(0, ctx_.render->backdrop_tile_count);
        for (std::size_t ti = static_cast<std::size_t>(n0);
             ti < ctx_.render->tiles.size(); ++ti) {
            const auto& tp = ctx_.render->tiles[ti];
            if (tp.sprite_idx < 0 ||
                tp.sprite_idx >=
                    static_cast<int>(ctx_.render->tile_sprites.size()))
                continue;
            presentation::blit_sprite(
                drt,
                ctx_.render->tile_sprites[static_cast<std::size_t>(
                    tp.sprite_idx)],
                ctx_.render->palette, tp.x, tp.y);
        }
    }
}

// Like wrap_wide, but the NO-NEIGHBOUR margins come from the STATIC
// background fill (compose_static_wide_bg_native — backdrop / ground / wide
// trunk extended to the edge, NO entities), with the live centre overlaid
// only in the centre 320.  wrap_wide's compose_widescreen mirrors the
// centre's edge columns into a no-neighbour margin — so a player standing at
// the screen edge (the L3 level-end exit at x~310) gets REFLECTED into the
// bezel as a ghost.  The static fill never contains entities, so no ghost;
// and it carries the same layer-extension the steady view uses.
void WidescreenPresenter::wrap_wide_static(const FrameBuffer& center,
                                           std::vector<std::uint8_t>& out) {
    const FrameBuffer* ws_bd =
        (backdrop_ok_ && ctx_.render->visual_background) ? &backdrop_
                                                         : nullptr;
    presentation::compose_static_wide_bg_native(
        *ctx_.state, *ctx_.render, margin_, left_ok_ ? &left_ : nullptr,
        right_ok_ ? &right_ : nullptr, ws_bd, out, left_seam_,
        right_seam_, left_bridge_, right_bridge_);
    for (int y = 0; y < 200; ++y)
        std::memcpy(
            &out[(static_cast<std::size_t>(y) * native_w_ + margin_) * 4],
            &center.px[static_cast<std::size_t>(y) * 320 * 4], 320 * 4);
    // The 320 overlay buried the seam tiles' centre-crossing parts (the
    // S1|S2 rail bridge, bush overhangs) — the cave-entry fade showed the
    // rail hole for the whole fade.  Re-apply them band-limited.
    reapply_seam_bands(out);
    // The 320 overlay just clobbered the L1-end void-water that sits inside
    // the centre region (the lake past the island); re-apply it so the
    // level-end fade's first frame shows the same continuous water as play.
    // Self-gating: no-op except on L1's last screen / its pseudo-exit.
    presentation::continue_l1_end_water(*ctx_.state, *ctx_.render,
                                        /*origin_x=*/margin_,
                                        /*buf_w=*/native_w_, out);
}

// Build the CURRENT screen's static wide margins EXACTLY like the steady
// view (get_static_wide_bg_hd): the SAME neighbour peeks (left_/right_ for
// the bound screen), so the margins are identical to the steady screen
// before and after the L3 trunk descent — no pop-in/out at the boundaries.
// (The descent presenter itself stays in game_app.cpp; this is its
// margins-builder over the presenter-owned cache.)
void WidescreenPresenter::compose_static_wide_bg(
    std::vector<std::uint8_t>& out) {
    const FrameBuffer* ws_bd =
        (backdrop_ok_ && ctx_.render->visual_background) ? &backdrop_
                                                         : nullptr;
    presentation::compose_static_wide_bg_native(
        *ctx_.state, *ctx_.render, margin_,
        left_ok_ ? &left_ : nullptr,
        right_ok_ ? &right_ : nullptr, ws_bd, out,
        left_seam_, right_seam_, left_bridge_, right_bridge_);
}

}  // namespace olduvai::presentation
