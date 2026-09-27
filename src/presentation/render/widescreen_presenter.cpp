// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Widescreen presenter: margins, peek cache, steady and transition presents.

#include "formats/hash64.hpp"
#include "presentation/diag/frame_stats.hpp" // FrameStats / note_present
#include "presentation/render/widescreen_presenter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "core/constants.hpp"
#include "enhance/upscale.hpp"
#include "presentation/render/boss_widescreen.hpp"   // boss_ws_margin (shared margin math)
#include "presentation/image_out.hpp"
#include "presentation/window_util.hpp"   // create_stream_tex
#include "presentation/render/text_overlay.hpp"
#include "presentation/render/tile_patterns.hpp"
#include "presentation/render/widescreen.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

namespace {
// OLDUVAI_DUMP_STEADY=<dir>: every steady widescreen present as a BMP (wide
// composite, before the text overlay).  With OLDUVAI_DUMP_TRANSITION it covers
// everything a widescreen session shows.
void dump_steady_wide(const std::uint8_t* px, int w, int h) {
    const char* dir = std::getenv("OLDUVAI_DUMP_STEADY");
    if (dir == nullptr) return;
    static int seq = 0;
    char name[32];
    char path[512];
    std::snprintf(name, sizeof name, "steady_ws_%04d.bmp", seq++);
    std::snprintf(path, sizeof path, "%s/%s", dir, name);
    save_rgba_image(px, w, h, path);
    note_dump_time(dir, name);
}
}  // namespace

// Margin M from the output aspect, the boss_ws_margin math (desired =
// 200*ow/oh, m = (desired-320)/2, capped 0..120, OLDUVAI_WS_FORCE_MARGIN
// override).  Used at level entry and on resize.  Warns once past the
// ultrawide cap (boss_ws_margin caps silently).
int WidescreenPresenter::compute_margin(int ow, int oh) const {
    if (*ctx_.aspect != "widescreen" || !hd() || ow <= 0 || oh <= 0)
        return 0;
    const char* fm = std::getenv("OLDUVAI_WS_FORCE_MARGIN");
    if (fm == nullptr) {
        // Tuned for ~16:9..21:9 (m up to ~73).  Past the cap (32:9) the wide
        // buffer no longer fills the display and side bars return; unsupported
        // but harmless.
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
    // OLDUVAI_WS_FORCE_MARGIN: a 16:10 panel derives margin 0, so headless
    // tests need this to reach the wide paths.
    return boss_ws_margin(ow, oh, fm);
}

// Log once when widescreen is asked for but the output is 16:10 or narrower
// (margin 0).
void WidescreenPresenter::note_no_margin_(int ow, int oh) {
    if (active_ || said_no_margin_ || *ctx_.aspect != "widescreen" || !hd())
        return;
    said_no_margin_ = true;
    std::fprintf(stderr,
                 "widescreen: this window is %dx%d (aspect %.2f) — at or "
                 "below the game's own 16:10, so there are no side margins "
                 "to show.  Try a wider window (--window 1680x720 is ~21:9) "
                 "or fullscreen on a wider display.\n",
                 ow, oh, oh > 0 ? static_cast<double>(ow) / oh : 0.0);
}

WidescreenPresenter::WidescreenPresenter(WidescreenShellCtx ctx)
    : ctx_(std::move(ctx)), applied_aspect_(*ctx_.aspect) {
    SDL_GetRendererOutputSize(ren(), &ow0_, &oh0_);
    margin_ = compute_margin(ow0_, oh0_);
    active_ = (*ctx_.aspect == "widescreen") && hd() && margin_ > 0;
    note_no_margin_(ow0_, oh0_);
    native_w_ = 320 + 2 * margin_;   // wide native width
}

void WidescreenPresenter::set_draw_overlay_tail(
    std::function<void(RenderTarget&)> fn) {
    ctx_.draw_overlay_tail = std::move(fn);
}

void WidescreenPresenter::set_banners_key(std::function<std::uint64_t()> fn) {
    ctx_.banners_key = std::move(fn);
}

void WidescreenPresenter::set_draw_banners(
    std::function<void(const enhance::Canvas&)> fn) {
    ctx_.draw_banners = std::move(fn);
}

// The wide foreground pass; the clip setup stays at the call sites (see the
// header).
void WidescreenPresenter::draw_wide_foreground(RenderTarget& wrt) {
    FrameStats::Timer fg(stats, &FrameStats::fg_ms);
    presentation::draw_entities(wrt, *ctx_.state, *ctx_.render,
                                /*draw_player=*/true);
    // L7 lava bubbles are reflected into the margin on purpose: unclip first.
    wrt.clip_x_lo = -(1 << 28);
    wrt.clip_x_hi = 1 << 28;
    presentation::draw_mirrored_lava_bubbles(
        wrt, *ctx_.state, *ctx_.render, /*mirror_left=*/!left_ok_,
        /*mirror_right=*/!right_ok_);
    if (ctx_.draw_overlay_tail) ctx_.draw_overlay_tail(wrt);
    draw_margin_monsters(wrt);   // Tier-1 living margins
}

// Everything draw_wide_hud_text puts in the overlay, as one key.  A zero banner
// key (wall-clock animation) forces kAlwaysRedraw.
std::uint64_t WidescreenPresenter::overlay_key(
    const enhance::EnhancedHudLayout& L, int ow, int oh) const {
    const std::uint64_t bk = ctx_.banners_key ? ctx_.banners_key()
                                              : TextOverlay::kAlwaysRedraw;
    if (bk == TextOverlay::kAlwaysRedraw) return TextOverlay::kAlwaysRedraw;
    formats::Hash64 key;
    const auto mix = [&key](std::uint64_t v) { key.mix(v); };
    mix(bk);
    mix(static_cast<std::uint64_t>(ow));
    mix(static_cast<std::uint64_t>(oh));
    mix(static_cast<std::uint64_t>(margin_));
    mix(static_cast<std::uint64_t>(native_w_));
    for (const auto& t : L.texts) {
        for (const char c : t.str) mix(static_cast<unsigned char>(c));
        mix(0x1ull);                       // separator: "ab","c" != "a","bc"
        mix(static_cast<std::uint64_t>(t.x));
        mix(static_cast<std::uint64_t>(t.baseline_y));
        mix((static_cast<std::uint64_t>(t.ink.r) << 16) |
            (static_cast<std::uint64_t>(t.ink.g) << 8) |
            static_cast<std::uint64_t>(t.ink.b));
    }
    // Never collide with the kAlwaysRedraw sentinel.
    const std::uint64_t h = key.value();
    return h == TextOverlay::kAlwaysRedraw ? 1ull : h;
}

void WidescreenPresenter::show_wide_with_hud(
    const enhance::EnhancedHudLayout* hud) const {
    FrameStats::Timer sc(stats, &FrameStats::scene_ms);
    ctx_.surface->show(wide_tex());
    if (hud == nullptr) return;
    // Keyed on the output size last seen (the overlay's own is known only
    // inside the pass; resizes are caught by sync_output and ensure()).
    ctx_.surface->overlay_pass(
        [&](const enhance::Canvas& cv) { draw_wide_hud_text(cv, *hud); },
        overlay_key(*hud, ow0_, oh0_));
}

void WidescreenPresenter::draw_wide_hud_text(
    const enhance::Canvas& cv,
    const enhance::EnhancedHudLayout& L) const {
    FrameStats::Timer gt(stats, &FrameStats::glyph_ms);
    const int cap = 8 * cv.w / native_w_;
    hd_text().set_cap_px(cap > 0 ? cap : 1);
    const double sx = static_cast<double>(cv.w) / native_w_;
    const double sy = cv.h / 200.0;
    for (const auto& t : L.texts) {
        const int x = static_cast<int>((t.x + margin_) * sx + 0.5);
        const int y = static_cast<int>(t.baseline_y * sy + 0.5);
        hd_text().draw(cv, x, y, t.str, t.ink);
    }
    // Banner substitutes; every widescreen present path routes here.
    if (ctx_.draw_banners) ctx_.draw_banners(cv);
}

void WidescreenPresenter::set_float_pos(bool use, float fx, float fy) {
    use_float_pos_ = use;
    player_fx_ = fx;
    player_fy_ = fy;
}

// A live Aspect edit (pause/title Video row) changes the aspect but not the
// output size, so rebuild_if_resized never sees it.
void WidescreenPresenter::sync_output() {
    const bool wide = *ctx_.aspect == "widescreen" && hd();
    if (*ctx_.aspect != applied_aspect_) {
        applied_aspect_ = *ctx_.aspect;
        ow0_ = -1;   // recompute below at the same output size
        oh0_ = -1;
        if (!wide) {
            active_ = false;
            margin_ = 0;
            native_w_ = 320;
            const LogicalDims ld = aspect_logical(hd_scale(), applied_aspect_);
            lsz().set(ld.w, ld.h);
            return;
        }
    }
    if (!wide) return;
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(ren(), &ow, &oh);
    if (ow == ow0_ && oh == oh0_) return;   // unchanged
    ow0_ = ow;
    oh0_ = oh;
    const bool was_active = active_;
    const int newM = compute_margin(ow, oh);
    if (newM != margin_) {
        margin_ = newM;
        native_w_ = 320 + 2 * margin_;
    }
    active_ = margin_ > 0;
    note_no_margin_(ow, oh);
    // Active: the wide canvas fills the output; inactive: the margin-0
    // logical ("widescreen" maps to keep).
    const LogicalDims fb = aspect_logical(hd_scale(), applied_aspect_);
    lsz().set(active_ ? native_w_ * hd_scale() : fb.w,
              active_ ? 200 * hd_scale() : fb.h);
    // Turned on mid-level: build the cache and backdrop the inactive level
    // entry skipped, before the next present.
    if (active_ && !was_active) refresh_level_state();
}

// Rebuild the level-derived state (FOND backdrop + peek cache) when
// widescreen turns on mid-level.
void WidescreenPresenter::refresh_level_state() {
    build_backdrop();
    update_cache();
}

// Peek neighbours for the current screen, composed once on screen bind: static
// (bg + terrain, no entities, no RNG) via compose_static, which never touches
// g.state or the RNG.
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
    // The current screen's straddling tiles reach into the neighbours: complete
    // them inside the peeks as underlay (over backdrop, under the neighbour's
    // tiles), not over the finished peek.
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
        // Authored seam holes: bridge a decor row broken at the seam (left
        // neighbour A, current B).  Tiles come back in A coordinates, as the
        // left seam list blits them at -320.
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
        // Current = A, right neighbour = B: returned in current-screen
        // coordinates; the right seam list is in neighbour coordinates (+320),
        // so shift by -320.
        for (const auto& b : presentation::tile_patterns::seam_row_bridges(
                 ctx_.render->tiles, ctx_.render->backdrop_tile_count,
                 nra.tiles, nra.backdrop_tile_count,
                 ctx_.render->tile_sprites))
            right_bridge_.push_back({b.sprite_idx, b.x - 320, b.y});
    }
}

// FOND backdrop (320x200 RGBA) for no-neighbour margins: the HUD-erased
// background through draw_bg_base's palette path.  Sky, mountains, clouds; no
// foreground tiles, so sampling its edge duplicates nothing.  Built once per
// level.  Gated on the LEVEL's flag, not the current screen: a level entered in
// a cave or secret room still needs it for later surface screens.
// ws_backdrop() withholds it where the current screen has no FOND.
void WidescreenPresenter::build_backdrop() {
    backdrop_ok_ = false;
    if (active_ && ctx_.level_visual_background &&
        ctx_.render->background.width == 320 &&
        ctx_.render->background.pixels.size() >= 320u * 200u) {
        const auto& bg = ctx_.render->background;
        indexed_to_rgba(bg.pixels, bg.palette, backdrop_.px.data(), 320u * 200u);
        backdrop_ok_ = true;
    }
}

// Null on screens without a FOND (secret rooms): compose_widescreen then
// self-tiles the room's own background.
const FrameBuffer* WidescreenPresenter::ws_backdrop() const {
    return (backdrop_ok_ && ctx_.render->visual_background) ? &backdrop_
                                                            : nullptr;
}

bool WidescreenPresenter::secret_selftile() const {
    return active_ && ctx_.state->secret_flag != 0 &&
           ctx_.state->current_screen >= 100;
}

// A surface screen on a peek level with no neighbour on either side still gets
// the wide fill (backdrop/floor extension).  Only the L3 end screen 18 (reached
// vertically, and the last screen).  Caves, secrets and bosses stay
// pillarboxed.
bool WidescreenPresenter::surface_selffill() const {
    return active_ && !left_ok_ && !right_ok_ &&
           ctx_.state->secret_flag == 0 && !ctx_.state->cave_flag &&
           ctx_.state->current_screen < 100 &&
           presentation::level_supports_peek(ctx_.internal_level_id);
}

// The one present-path predicate (peek, secret self-tile, or no-neighbour
// surface fill), shared by every present call site, so the overflow pass and
// the single club_flag advance cannot disagree between sites.
bool WidescreenPresenter::present_path() const {
    return active_ &&
           (left_ok_ || right_ok_ || secret_selftile() || surface_selffill());
}

// Margin monsters: cycle walk sprites in place once per tick.  No movement,
// RNG or collision; the spawn-post anchor keeps entry pop-free.  L3A alternates
// follow the global phase counter.
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

// Draw the margin monsters on a wide target (origin_x = margin_).  Lists are in
// neighbour coordinates (-320 / +320); each side is clipped to its margin so
// nothing draws over the live centre.
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

// Steady widescreen present: compose the centre background (no entities),
// assemble it with the cached margins, then draw the live foreground once at
// origin_x = margin_ so sprites crossing the 320 edge overflow into the
// margins.  advance_state=false: the single state advance is the main fb
// compose, before the smooth-motion save/restore.  fb is not reused here: its
// entities are clipped at 320.  The vector HUD text maps to the centre 320.

void WidescreenPresenter::present(
    const std::function<void(RenderTarget&)>& bubble_hook_w,
    bool do_present) {
    FrameStats::Timer pt(stats, &FrameStats::present_ms);
    if (stats != nullptr) stats->note_present();
    sync_output();   // Alt+Enter, resize or a live Aspect edit
    // The layout only: the state-mutating HUD draw already ran on `fb` (a
    // second would double-decrement GET READY).
    const auto hud = ctx_.surface->hud_layout(*ctx_.state);
    const enhance::EnhancedHudLayout* const hud_p = hud ? &*hud : nullptr;
    if (hd() && hd_scale() > 1 && wide_tex() != nullptr)
        present_fast(bubble_hook_w, hud_p);
    else
        present_slow(bubble_hook_w, hud_p);
    show_wide_with_hud(hud_p);
    // do_present=false leaves the frame for a RenderReadPixels (black on Metal
    // after present).
    if (do_present) flip();
}

RenderTarget WidescreenPresenter::foreground_target(
    std::uint8_t* px, int w, int h, int scale, enhance::HdAssetCache* cache,
    const std::string* profile) const {
    RenderTarget wrt{px, w, h, scale, cache, profile};
    wrt.origin_x = margin_;
    wrt.advance_state = false;
    wrt.use_float_pos = use_float_pos_;
    wrt.player_fx = player_fx_;
    wrt.player_fy = player_fy_;
    return wrt;
}

void WidescreenPresenter::clip_foreground(RenderTarget& wrt) const {
    const int s = wrt.scale;
    const systems::SystemsState& st = *ctx_.state;
    // Secret room: the floor clip draw_background sets on its own target
    // (FUN_1052_2813), or the trampoline springs poke over the floor.
    if (st.secret_flag) wrt.clip_y = (168 + 1) * s;
    // The level edge has nothing beyond it: the player is clipped there (it
    // would spill onto the synthetic fill as a mirrored player); a real
    // neighbour keeps the overflow, and so do flying entities.  Secret rooms
    // are exempt (their self-tiled margins continue the room), and the L5
    // glider fly-away off the last screen overflows right, like the L4
    // ride-off.
    if (st.secret_flag != 0) return;
    const bool glider_flyoff = st.current_level == 5 && st.glider_active &&
                               st.current_screen == core::kLastScreen;
    if (!left_ok_) wrt.player_clip_x_lo = margin_ * s;
    if (!right_ok_ && !glider_flyoff) wrt.player_clip_x_hi = (margin_ + 320) * s;
}

void WidescreenPresenter::flip() const {
    FrameStats::Timer sw(stats, &FrameStats::swap_ms);   // the vsync block
    present_output(ren());
}

// The static wide background is fixed per screen: cached upscaled
// (get_static_wide_bg_hd), with only sprites and HUD bars drawn at HD per
// frame (omniscale 36 ms -> ~3 ms).  Secret rooms too: bubbles over the
// cached bg, then redraw_bg_tiles puts the floor back on top.
void WidescreenPresenter::present_fast(
    const std::function<void(RenderTarget&)>& bubbles,
    const enhance::EnhancedHudLayout* hud) {
    const FrameBuffer* ws_bd = ws_backdrop();
    // `peek` is a named local, not a braced temporary: GCC 13-15
    // -Wdangling-reference fires on the temporary (a false positive; the
    // result lives in a static cache), and CI builds with -Werror.
    const WidePeek peek{left_ok_ ? &left_ : nullptr, left_screen_,
                        right_ok_ ? &right_ : nullptr, right_screen_,
                        ws_bd, &left_seam_, &right_seam_, &left_bridge_,
                        &right_bridge_, peek_generation_};
    const std::vector<std::uint8_t>& bg_hd = presentation::get_static_wide_bg_hd(
        *ctx_.state, *ctx_.render, hd_scale(), *hd_profile(), margin_, peek);
    const int uw = native_w_ * hd_scale(), uh = 200 * hd_scale();
    const std::size_t n = static_cast<std::size_t>(uw) * uh * 4;
    if (frame_hd_.size() != n) frame_hd_.resize(n);
    {
        FrameStats::Timer bgc(stats, &FrameStats::bg_copy_ms);
        std::memcpy(frame_hd_.data(), bg_hd.data(), std::min(n, bg_hd.size()));
    }
    {
        RenderTarget wrt = foreground_target(frame_hd_.data(), uw, uh,
                                             hd_scale(), ctx_.hd_cache,
                                             hd_profile());
        // Secret room: bubbles over the cached bg, then the floor tiles on
        // top (draw_background's base -> bubbles -> tiles order), both before
        // the floor clip.
        if (bubbles) {
            bubbles(wrt);
            presentation::redraw_bg_tiles(wrt, *ctx_.state, *ctx_.render);
        }
        clip_foreground(wrt);
        draw_wide_foreground(wrt);
    }
    if (hud != nullptr)   // the bars at HD, shifted to the centre
        enhance::draw_enhanced_hud_bars({frame_hd_, uw, uh}, hd_scale(), *hud,
                                        margin_);
    dump_steady_wide(frame_hd_.data(), uw, uh);
    FrameStats::Timer ut(stats, &FrameStats::upload_ms);
    ctx_.surface->upload(frame_hd_, native_w_, LevelSurface::Res::kHd);
}

// The centre gets background and tiles only (this pass mutates nothing),
// neighbour terrain goes in the margins, then the foreground once over the
// wide buffer, and the whole of it is upscaled.
void WidescreenPresenter::present_slow(
    const std::function<void(RenderTarget&)>& bubbles,
    const enhance::EnhancedHudLayout* hud) {
    FrameBuffer center{};   // 320x200
    {
        RenderTarget rt{center.px.data(), 320, 200, 1, nullptr, nullptr};
        presentation::draw_background(rt, *ctx_.state, *ctx_.render, bubbles);
    }
    if (hud != nullptr)
        enhance::draw_enhanced_hud_bars({center.px, 320, 200}, 1, *hud);
    // backdrop_ok_ stays true in a secret room, which has no FOND;
    // ws_backdrop() gates on the current screen so surface mountains do not
    // bleed in.
    std::vector<std::uint8_t> wide;
    presentation::compose_widescreen(
        wide, margin_, center, left_ok_ ? &left_ : nullptr,
        right_ok_ ? &right_ : nullptr,
        MarginFill{/*hud_rows=*/0, ws_backdrop(),
                   /*reflect_pure=*/false, /*margin_edge_brightness=*/1.0f,
                   /*repeat_no_backdrop=*/ctx_.state->secret_flag == 0});
    {
        RenderTarget wrt =
            foreground_target(wide.data(), native_w_, 200, 1, nullptr, nullptr);
        clip_foreground(wrt);
        draw_wide_foreground(wrt);
    }
    dump_steady_wide(wide.data(), native_w_, 200);
    const std::vector<std::uint8_t> up = enhance::upscale_rgba(
        wide, native_w_, 200, hd_scale(), *hd_profile());
    FrameStats::Timer ut(stats, &FrameStats::upload_ms);
    ctx_.surface->upload(up, native_w_, LevelSurface::Res::kHd);
}

// Present a wide native buffer (no baked HUD) through the wide texture, with
// the HUD fixed over the centre 320, so a transition keeps the steady frame's
// width and HUD position.  The sim is paused: the HUD layout is read-only.
void WidescreenPresenter::present_transition(std::vector<std::uint8_t>& wide,
                                             bool with_hud,
                                             bool pre_upscaled,
                                             bool do_present) const {
    FrameStats::Timer pt(stats, &FrameStats::present_ms);
    if (stats != nullptr) stats->note_present();
    if (stats != nullptr && stats->enabled) stats->tick_paused = true;
    // No rebuild_if_resized() here: `wide` is sized at the current native_w()
    // and reused across the animation; a mid-transition Alt+Enter is picked up
    // by the next steady present. pre_upscaled: `wide` is already HD (the
    // panorama pan upscales its strip once).
    std::vector<std::uint8_t> up;
    if (!pre_upscaled)
        up = enhance::upscale_rgba(wide, native_w_, 200, hd_scale(),
                                   *hd_profile());
    std::vector<std::uint8_t>& hdbuf = pre_upscaled ? wide : up;
    // HUD bars at the centre offset (margin_*hd_scale), fixed while the centre
    // slides.  with_hud=false: no HUD (the level-end fade darkens it with the
    // scene).
    const auto hud =
        with_hud ? ctx_.surface->hud_layout(*ctx_.state) : std::nullopt;
    if (hud) {
        // draw_enhanced_hud_bars expects a 320*scale buffer: draw into a centre
        // strip, copy back.
        const int cw = 320 * hd_scale();   // centre width in output px
        const int ch = 200 * hd_scale();
        const int cx = margin_ * hd_scale(); // centre x-origin in wide output
        std::vector<std::uint8_t> centre(
            static_cast<std::size_t>(cw) * ch * 4);
        for (int y = 0; y < ch; ++y)
            std::copy_n(hdbuf.begin() +
                            (static_cast<std::size_t>(y) * native_w_ *
                                 hd_scale() + cx) * 4,
                        static_cast<std::size_t>(cw) * 4,
                        centre.begin() +
                            static_cast<std::size_t>(y) * cw * 4);
        enhance::draw_enhanced_hud_bars({centre, cw, ch}, hd_scale(), *hud);
        for (int y = 0; y < ch; ++y)
            std::copy_n(centre.begin() +
                            static_cast<std::size_t>(y) * cw * 4,
                        static_cast<std::size_t>(cw) * 4,
                        hdbuf.begin() +
                            (static_cast<std::size_t>(y) * native_w_ *
                                 hd_scale() + cx) * 4);
    }
    // OLDUVAI_WIDE_TRANSITION_DUMP=<dir>: the wide HD buffer after the HUD-bar
    // splice, before upload (no vector text: float-rasterized, not
    // hash-stable). Hashed by tests/wide_transition.sh; needs an integer
    // hd_profile (mmpx/xbr).
    if (const char* wd = std::getenv("OLDUVAI_WIDE_TRANSITION_DUMP")) {
        static int wtseq = 0;
        char wtpath[512];
        std::snprintf(wtpath, sizeof wtpath, "%s/wpresent_%04d.png", wd,
                      wtseq++);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
            hdbuf.data(), native_w_ * hd_scale(), 200 * hd_scale(), 32,
            native_w_ * hd_scale() * 4, SDL_PIXELFORMAT_RGBA32);
        if (s) { save_surface_image(s, wtpath); SDL_FreeSurface(s); }
    }
    { FrameStats::Timer ut(stats, &FrameStats::upload_ms);
    ctx_.surface->upload(hdbuf, native_w_, LevelSurface::Res::kHd); }
    show_wide_with_hud(hud ? &*hud : nullptr);
    // The vsync block timed on its own: a whole transition runs inside one
    // main-loop iteration, so its waits would all land in that present_ms.
    if (do_present) flip();
}

// Wrap a native 320 centre (no baked HUD) in the current cache.  The caller
// wraps the outgoing frame before the rebind and the incoming one after.
// Same margin rules as present(); hud_rows 0.  No state mutation, no RNG.
void WidescreenPresenter::wrap_wide(const FrameBuffer& center,
                                    std::vector<std::uint8_t>& out) {
    const FrameBuffer* ws_bd = ws_backdrop();
    presentation::compose_widescreen(
        out, margin_, center,
        left_ok_ ? &left_ : nullptr,
        right_ok_ ? &right_ : nullptr,
        MarginFill{/*hud_rows=*/0, ws_bd,
                   /*reflect_pure=*/false, /*margin_edge_brightness=*/1.0f,
                   /*repeat_no_backdrop=*/ctx_.state->secret_flag == 0});
}

// Wrap a 320 centre with black margins, for a transition side that is not a
// present_path screen (a cave), keeping the centre and HUD where the peek side
// has them.
void WidescreenPresenter::wrap_wide_bezel(
    const FrameBuffer& center, std::vector<std::uint8_t>& out) const {
    // Pure black: dark gray looks gray next to a cave's black tiles.
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

// Re-apply seam content (straddlers, row bridges) that a 320 centre copy just
// covered (a bush overhang, the S1|S2 rail bridge at x=304..320), limited to
// the tiles' extents, then the screen's own tiles on top.
void WidescreenPresenter::reapply_seam_bands(std::vector<std::uint8_t>& wide,
                                             bool centre_has_entities,
                                             bool draw_player) {
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
        // If the centre already had entities, redraw them: the tile pass
        // painted over them (L1 S2 lost its two spikes at x=40/60 during the
        // cave fade). advance_state=false: same tick.
        if (centre_has_entities) {
            drt.advance_state = false;
            presentation::draw_entities(drt, *ctx_.state, *ctx_.render,
                                        draw_player);
        }
    }
}

// Like wrap_wide, but no-neighbour margins come from the static background
// fill (no entities), so a player at the edge (L3 exit, x~310) is not mirrored
// into the bezel.
void WidescreenPresenter::wrap_wide_static(const FrameBuffer& center,
                                           std::vector<std::uint8_t>& out) {
    // Same margins method as the steady view and the descent.
    compose_static_wide_bg(out);
    for (int y = 0; y < 200; ++y)
        std::memcpy(
            &out[(static_cast<std::size_t>(y) * native_w_ + margin_) * 4],
            &center.px[static_cast<std::size_t>(y) * 320 * 4], 320 * 4);
    // Re-apply the seam tiles the 320 overlay covered (the cave-entry fade
    // showed the rail hole).  draw_player=false: the centre already has the
    // outgoing player (the descent sprite on cave entry); a second, standing
    // one would appear.
    reapply_seam_bands(out, /*centre_has_entities=*/true,
                       /*draw_player=*/false);
    // Re-apply the L1-end water inside the centre region (no-op elsewhere).
    presentation::continue_l1_end_water(*ctx_.state, *ctx_.render,
                                        /*origin_x=*/margin_,
                                        /*buf_w=*/native_w_, out);
}

// The current screen's static wide margins, built exactly like the steady view
// (same neighbour peeks), so the L3 trunk descent has no margin pop.
void WidescreenPresenter::compose_static_wide_bg(
    std::vector<std::uint8_t>& out) {
    const FrameBuffer* ws_bd = ws_backdrop();
    presentation::compose_static_wide_bg_native(
        *ctx_.state, *ctx_.render, margin_,
        left_ok_ ? &left_ : nullptr,
        right_ok_ ? &right_ : nullptr, ws_bd, out,
        SeamTiles{left_seam_, right_seam_, left_bridge_, right_bridge_});
}

}  // namespace olduvai::presentation
