// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Blocking transition players (pan, fade, secret slides, wide and panorama
// variants).  Contract: transition_players.hpp.
#include "presentation/sequence/transition_players.hpp"

#include "presentation/sequence/secret_slide.hpp"
#include "presentation/sequence/transition_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "core/constants.hpp"
#include "enhance/incremental_upscale.hpp"   // LazyUpscaler
#include "enhance/upscale.hpp"
#include "presentation/image_out.hpp"
#include "presentation/sequence/screens.hpp"
#include "presentation/render/shift_blit.hpp"
#include "presentation/render/tile_patterns.hpp"
#include "presentation/render/widescreen.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::presentation {

namespace {

// Step pacing: keep steps step_ms apart, absorbing compose + present time
// (including a vsync block), so a transition lasts the same with or without
// vsync.  Never delays longer than SDL_Delay(step_ms); a long gap resets.
// ctx.pace_last starts at 0 per ctx.
void paced(TransitionShellCtx& ctx, Uint32 step_ms) {
    const Uint32 now = SDL_GetTicks();
    if (ctx.pace_last != 0 && now - ctx.pace_last < step_ms)
        SDL_Delay(step_ms - (now - ctx.pace_last));
    ctx.pace_last = SDL_GetTicks();
}

// Wall-clock progress for smooth transitions (PARITY T8): the elapsed fraction
// of `dur`, 1.0 once past, floored at `lo` so a pan's first frame moves.  A
// slow present drops frames instead of stretching the transition (the A12's ~25
// ms present held the cave fade at ~2.6 s instead of ~1.2 s).  Classic keeps
// the exact integer frame counts.
double wall_progress(Uint32 t0, Uint32 dur, double lo = 0.0) {
    const Uint32 elapsed = SDL_GetTicks() - t0;
    if (elapsed >= dur) return 1.0;
    return std::max(lo, static_cast<double>(elapsed) / dur);
}

Uint32 step_ms_of(const TransitionShellCtx& ctx) {
    return ctx.smooth_motion ? (1000 / 60) : ctx.frame_ms;
}

// Pace the step, then poll: false (and *running cleared) on a window close.
bool step_done(TransitionShellCtx& ctx, Uint32 step_ms) {
    paced(ctx, step_ms);
    if (poll_screen_events(ctx.win)) return true;
    *ctx.running = false;
    return false;
}

// A pan's position: classic steps pos = f2 = 1..total and p = f2 / total;
// smooth reads p off the wall clock, floored at one frame, and pos = p * total.
struct PanStep {
    double pos;
    double p;
    int f2;
};

// Run `frame` over a `total`-frame pan, pacing and polling after each.  False
// = the window closed.
template <class Frame>
bool run_pan(TransitionShellCtx& ctx, int total, Frame&& frame) {
    const Uint32 step_ms = step_ms_of(ctx);
    const Uint32 t0 = SDL_GetTicks();
    const Uint32 dur = static_cast<Uint32>(total) * step_ms;
    for (int f2 = 1;; ++f2) {
        const double p = ctx.smooth_motion
                             ? wall_progress(t0, dur, 1.0 / total)
                             : static_cast<double>(f2) / total;
        const double pos = ctx.smooth_motion ? p * total : f2;
        frame(PanStep{pos, p, f2});
        if (!step_done(ctx, step_ms)) return false;
        if (ctx.smooth_motion ? p >= 1.0 : f2 >= total) return true;
    }
}

// kind 2: fade the old frame out, then the new one in.  Classic: n + 1 frames
// each way; smooth: the same span on the wall clock.  `show` presents `work`.
template <class Show>
bool play_fade(TransitionShellCtx& ctx, FrameBuffer& work,
               const FrameBuffer& oldf, const FrameBuffer& newf, Show&& show) {
    const Uint32 step_ms = step_ms_of(ctx);
    const int n = ctx.smooth_motion ? 36 : kFadeFrames;
    const auto step = [&](const FrameBuffer& src, double level) {
        apply_fade(work, src, level);
        show(work);
        return step_done(ctx, step_ms);
    };
    if (ctx.smooth_motion) {
        const Uint32 dur = static_cast<Uint32>(n + 1) * step_ms;
        for (const bool out : {true, false}) {
            const Uint32 t0 = SDL_GetTicks();
            for (;;) {
                const double p = wall_progress(t0, dur);
                if (!step(out ? oldf : newf, out ? p : 1.0 - p)) return false;
                if (p >= 1.0) break;
            }
        }
        return true;
    }
    for (int f = 0; f <= n; ++f)
        if (!step(oldf, static_cast<double>(f) / n)) return false;
    for (int f = n; f >= 0; --f)
        if (!step(newf, static_cast<double>(f) / n)) return false;
    return true;
}

// OLDUVAI_DUMP_TRANSITION=<dir>: every frame of one player as
// <stem>[_k<kind>]_NNNN.bmp (kind < 0: no kind); `seq` numbers them.
void dump_frame(const char* stem, int kind, int& seq,
                const std::uint8_t* px, int w, int h) {
    const char* dir = std::getenv("OLDUVAI_DUMP_TRANSITION");
    if (dir == nullptr) return;
    char path[512];
    if (kind < 0)
        std::snprintf(path, sizeof path, "%s/%s_%04d.bmp", dir, stem, seq++);
    else
        std::snprintf(path, sizeof path, "%s/%s_k%d_%04d.bmp", dir, stem,
                      kind, seq++);
    save_rgba_image(px, w, h, path);
}

}  // namespace

void play_transition(TransitionShellCtx& ctx, const FrameBuffer& oldf,
                     FrameBuffer& newf, TransitionKind kind, char dir) {
    static int seq = 0;
    // Work buffer: the source buffers' size (HD or native).
    FrameBuffer work{oldf.w, oldf.h};
    const auto show = [&](FrameBuffer& f) {
        ctx.upload_and_show(f);
        dump_frame("trans", static_cast<int>(kind), seq, f.px.data(), f.w, f.h);
    };
    if (kind == TransitionKind::kFadePair) {
        play_fade(ctx, work, oldf, newf, show);
        return;
    }
    // The surface pan (in `dir`) and the secret slides share one shift core
    // (transition_geometry.hpp); distances follow the buffer size.
    const bool is_slide = is_secret_slide(kind);
    const SecretSlide slide(kind, ctx.landing, ctx.state->player.facing_left);
    const int n = is_slide ? slide.frames()
                           : (ctx.smooth_motion ? 36 : 12);   // SCROLL_FRAMES
    run_pan(ctx, is_slide ? slide.total() : n, [&](const PanStep& s) {
        const double t = std::min(1.0, s.pos / n);
        const TransitionShift sh = transition_shift(kind, dir, t, work.w, work.h);
        clear_opaque(work.px);
        blit_shifted(work.px, oldf.px, work.w, work.h, sh.odx, sh.ody);
        blit_shifted(work.px, newf.px, work.w, work.h, sh.ndx, sh.ndy);
        if (slide.has_arc()) {
            auto rt = ctx.make_rt(work);
            slide.draw_arc(rt, *ctx.render, s.pos, s.f2, ctx.draw_log);
        }
        show(work);
    });
}

// Widescreen transitions: the kind 1 pan, kind 2 fade and the secret slides
// over wide buffers, presented through wsp->present_transition, so width and
// HUD position match the steady frame.  oldw / neww are pre-wrapped (peek or
// bezel per side), upscaled once here; shifts are native, scaled per frame.
void play_transition_wide(TransitionShellCtx& ctx,
                          std::vector<std::uint8_t>& oldw,
                          std::vector<std::uint8_t>& neww, TransitionKind kind,
                          char dir) {
    static int seq = 0;
    const int s = ctx.hd_scale;
    const int W = ctx.wsp->native_w(), H = 200;
    const int Wh = W * s, Hh = H * s;
    FrameBuffer work{Wh, Hh}, hd_old{Wh, Hh}, hd_new{Wh, Hh};
    hd_old.px = enhance::upscale_rgba(oldw, W, H, s, *ctx.hd_profile);
    hd_new.px = enhance::upscale_rgba(neww, W, H, s, *ctx.hd_profile);
    const auto show = [&](FrameBuffer& f) {
        ctx.wsp->present_transition(f.px, /*with_hud=*/true,
                                    /*pre_upscaled=*/true);
        dump_frame("wtrans", static_cast<int>(kind), seq, f.px.data(), Wh,
                   Hh);
    };
    if (kind == TransitionKind::kFadePair) {
        play_fade(ctx, work, hd_old, hd_new, show);
        return;
    }
    const auto shifted = [&](const TransitionShift& sh) {
        clear_opaque(work.px);
        blit_shifted(work.px, hd_old.px, Wh, Hh, sh.odx * s, sh.ody * s);
        blit_shifted(work.px, hd_new.px, Wh, Hh, sh.ndx * s, sh.ndy * s);
    };
    if (is_secret_slide(kind)) {
        // Vertical only; the arc sprite sits at +wsp->margin().
        const SecretSlide slide(kind, ctx.landing,
                                ctx.state->player.facing_left);
        run_pan(ctx, slide.total(), [&](const PanStep& st) {
            const double t = std::min(1.0, st.pos / slide.frames());
            TransitionShift sh = transition_shift(kind, dir, t, W, H);
            sh.odx = sh.ndx = 0;
            shifted(sh);
            if (slide.has_arc()) {
                RenderTarget rt{work.px.data(), Wh, Hh, s, ctx.hd_cache,
                                ctx.hd_profile};
                rt.origin_x = ctx.wsp->margin();
                slide.draw_arc(rt, *ctx.render, st.pos, st.f2, nullptr);
            }
            show(work);
        });
        return;
    }
    // The pan over the WHOLE wide view.
    const int n = ctx.smooth_motion ? 36 : 12;   // SCROLL_FRAMES
    run_pan(ctx, n, [&](const PanStep& st) {
        shifted(transition_shift(kind, dir, std::min(1.0, st.p), W, H));
        show(work);
    });
}

// Widescreen panorama pan (kind 1): slide a (320+2M) window across one strip
// of four screens [min-1 | min | min+1 | min+2] by exactly 320.  The margins
// are the real adjacent screens at every instant (sliding two separately
// composed wide buffers over-scrolls by 2M and tears).  The incoming screen
// carries the player; off-level slots get an edge fill.
constexpr int kStripW = 4 * 320;
constexpr int kStripH = 200;

namespace {

// Blit each real slot's straddling edge tiles across its boundary, recording
// the strip-x [lo, hi) bands they land on (the later passes are limited to
// them).  Slots 1 and 2 are always real.
void pan_bridge_collect_overhangs(
    TransitionShellCtx& ctx, int lo, bool slot0_real, bool slot3_real,
    presentation::RenderTarget& srt,
    std::vector<std::pair<int, presentation::LevelRenderAssets>>& slot_assets,
    std::vector<std::pair<int, int>>& bands) {
    std::vector<std::pair<int, int>> real_slots = {{1, lo + 1}, {2, lo + 2}};
    if (slot0_real) real_slots.emplace_back(0, lo);
    if (slot3_real) real_slots.emplace_back(3, lo + 3);
    for (const auto& slot_scr : real_slots) {
        // Named locals: capturing a structured binding needs C++20.
        const int slot = slot_scr.first;
        const int scr = slot_scr.second;
        presentation::LevelRenderAssets ra;
        systems::SystemsState sst;
        ctx.build_assets(scr, ra, sst);
        auto spill = [&](bool right_edge) {
            srt.origin_x = slot * 320;
            srt.clip_x_lo = right_edge ? (slot + 1) * 320 : -(1 << 28);
            srt.clip_x_hi = right_edge ? (1 << 28) : slot * 320;
            int ext_lo = 1 << 28, ext_hi = -(1 << 28);
            for (const auto& tp :
                 presentation::tile_patterns::seam_straddling_tiles(
                     ra.tiles, ra.tile_sprites, right_edge)) {
                if (tp.sprite_idx < 0 ||
                    tp.sprite_idx >= static_cast<int>(ra.tile_sprites.size()))
                    continue;
                const auto& spr =
                    ra.tile_sprites[static_cast<std::size_t>(tp.sprite_idx)];
                presentation::blit_sprite(srt, spr, ra.palette, tp.x, tp.y);
                const int sx = slot * 320;
                if (right_edge) {
                    ext_lo = std::min(ext_lo, sx + 320);
                    ext_hi = std::max(ext_hi, sx + tp.x + spr.width);
                } else {
                    ext_lo = std::min(ext_lo, sx + tp.x);
                    ext_hi = std::max(ext_hi, sx);
                }
            }
            if (ext_hi > ext_lo) bands.emplace_back(ext_lo, ext_hi);
        };
        spill(/*right_edge=*/true);
        spill(/*right_edge=*/false);
        slot_assets.emplace_back(slot, std::move(ra));
    }
}

}  // namespace

// Seam continuity across the strip, same laws as the steady compose: a
// straddling trunk/pillar must not cut at a slot boundary.  Each real slot's
// straddlers spill into the adjacent slot, clipped from their own.  The slots
// are full composes with entities and the baked player, so the authored-tile
// redraw is limited to the overhang bands, and the player box is restored from
// new_center last.
void pan_bridge_seams(TransitionShellCtx& ctx,
                      std::vector<std::uint8_t>& strip, int lo, int new_s,
                      const FrameBuffer& new_center, bool slot0_real,
                      bool slot3_real) {
    {
        std::vector<std::pair<int, presentation::LevelRenderAssets>>
            slot_assets;
        std::vector<std::pair<int, int>> bands;   // strip-x [lo, hi)
        presentation::RenderTarget srt{strip.data(), kStripW, kStripH, 1,
                                       nullptr, nullptr};
        pan_bridge_collect_overhangs(ctx, lo, slot0_real, slot3_real, srt,
                                     slot_assets, bands);
        // Seam-hole bridges between adjacent real slots (as the steady view:
        // the L7 S1|S2 jumppad rail).  They join the bands.
        for (const auto& [sa, ra_a] : slot_assets)
            for (const auto& [sb, ra_b] : slot_assets) {
                if (sb != sa + 1) continue;
                srt.origin_x = sa * 320;
                srt.clip_x_lo = -(1 << 28);
                srt.clip_x_hi = 1 << 28;
                int ext_lo = 1 << 28, ext_hi = -(1 << 28);
                for (const auto& tb :
                     presentation::tile_patterns::seam_row_bridges(
                         ra_a.tiles, ra_a.backdrop_tile_count,
                         ra_b.tiles, ra_b.backdrop_tile_count,
                         ra_a.tile_sprites)) {
                    if (tb.sprite_idx < 0 ||
                        tb.sprite_idx >=
                            static_cast<int>(ra_a.tile_sprites.size()))
                        continue;
                    const auto& spr =
                        ra_a.tile_sprites[static_cast<std::size_t>(
                            tb.sprite_idx)];
                    presentation::blit_sprite(srt, spr, ra_a.palette,
                                              tb.x, tb.y);
                    ext_lo = std::min(ext_lo, sa * 320 + tb.x);
                    ext_hi = std::max(ext_hi,
                                      sa * 320 + tb.x + spr.width);
                }
                if (ext_hi > ext_lo) bands.emplace_back(ext_lo, ext_hi);
            }
        // Authored tiles win — but ONLY inside the received bands.
        for (const auto& [blo, bhi] : bands) {
            for (const auto& [slot, ra] : slot_assets) {
                const int slo = std::max(blo, slot * 320);
                const int shi = std::min(bhi, (slot + 1) * 320);
                if (slo >= shi) continue;
                srt.origin_x = slot * 320;
                srt.clip_x_lo = slo;
                srt.clip_x_hi = shi;
                const int n0 = std::max(0, ra.backdrop_tile_count);
                for (std::size_t ti = static_cast<std::size_t>(n0);
                     ti < ra.tiles.size(); ++ti) {
                    const auto& tp = ra.tiles[ti];
                    if (tp.sprite_idx < 0 ||
                        tp.sprite_idx >=
                            static_cast<int>(ra.tile_sprites.size()))
                        continue;
                    presentation::blit_sprite(
                        srt,
                        ra.tile_sprites[static_cast<std::size_t>(
                            tp.sprite_idx)],
                        ra.palette, tp.x, tp.y);
                }
            }
        }
        // Restore the player's box from new_center last: the player draws above
        // all.
        if (!bands.empty()) {
            const int pslot = new_s - lo;
            const int bx0 = std::max(0, ctx.state->player.x - 16);
            const int bx1 = std::min(320, ctx.state->player.x + 48);
            const int by0 = std::max(0, ctx.state->player.y - 24);
            const int by1 = std::min(kStripH, ctx.state->player.y + 48);
            for (int y = by0; y < by1; ++y)
                std::memcpy(
                    &strip[(static_cast<std::size_t>(y) * kStripW +
                            static_cast<std::size_t>(pslot) * 320 +
                            bx0) * 4],
                    &new_center.px[(static_cast<std::size_t>(y) * 320 +
                                    bx0) * 4],
                    static_cast<std::size_t>(bx1 - bx0) * 4);
        }
    }
}

namespace {

// One 320-wide frame into strip slot `slot`.
void put_slot(std::vector<std::uint8_t>& strip, int slot,
              const FrameBuffer& src) {
    for (int y = 0; y < kStripH; ++y)
        std::copy_n(src.px.begin() + static_cast<std::size_t>(y) * 320 * 4,
                    320 * 4,
                    strip.begin() + (static_cast<std::size_t>(y) * kStripW +
                                     static_cast<std::size_t>(slot) * 320) * 4);
}

// Off-level, FOND levels: the adjacent screen mirrored across its edge
// (column x <- 319 - x); fond_sky_band then replaces the sky.
void mirror_slot(TransitionShellCtx& ctx, std::vector<std::uint8_t>& strip,
                 int slot, int adj_s) {
    FrameBuffer t{};
    ctx.compose_static(adj_s, t, /*frozen_full=*/false);
    for (int y = 0; y < kStripH; ++y)
        for (int x = 0; x < 320; ++x) {
            const std::uint8_t* e =
                &t.px[(static_cast<std::size_t>(y) * 320 + (319 - x)) * 4];
            std::uint8_t* d = &strip[(static_cast<std::size_t>(y) * kStripW +
                                      static_cast<std::size_t>(slot) * 320 +
                                      x) * 4];
            d[0] = e[0]; d[1] = e[1]; d[2] = e[2]; d[3] = 255;
        }
}

// FOND levels (1/5): the sky band from the real FOND, as the steady margin
// samples it; the ground band stays.
void fond_sky_band(TransitionShellCtx& ctx, std::vector<std::uint8_t>& strip,
                   int slot) {
    if (!ctx.wsp->backdrop_ok()) return;
    const int gb = kStripH - presentation::kWideGroundBandRows;
    for (int y = 0; y < gb; ++y)
        std::memcpy(&strip[(static_cast<std::size_t>(y) * kStripW +
                            static_cast<std::size_t>(slot) * 320) * 4],
                    &ctx.wsp->backdrop().px[static_cast<std::size_t>(y) * 320 * 4],
                    320 * 4);
}

// Off-level, tile levels: exactly the steady margin.  The adjacent screen's
// wide static bg, composed as the steady view does; its outer margin goes
// into the slot's inner M columns (all the window shows).  side < 0: left
// slot (right M columns <- the left margin); side > 0: the reverse.  No pop
// at the pan -> steady hand-off.
void steady_margin_slot(TransitionShellCtx& ctx,
                        std::vector<std::uint8_t>& strip, int slot, int adj_s,
                        int side) {
    const int M = ctx.wsp->margin();
    const FrameBuffer* bd =
        (ctx.wsp->backdrop_ok() && ctx.render->visual_background)
            ? &ctx.wsp->backdrop()
            : nullptr;
    std::vector<std::uint8_t> wide;
    ctx.compose_wide_native(adj_s, M, bd, wide);
    const int wide_w = 320 + 2 * M;
    for (int y = 0; y < kStripH; ++y)
        for (int x = 0; x < M; ++x) {
            const int sc = (side < 0) ? x : (M + 320 + x);
            const int dc = (side < 0) ? (320 - M + x) : x;
            const std::uint8_t* e =
                &wide[(static_cast<std::size_t>(y) * wide_w + sc) * 4];
            std::uint8_t* d = &strip[(static_cast<std::size_t>(y) * kStripW +
                                      static_cast<std::size_t>(slot) * 320 +
                                      dc) * 4];
            d[0] = e[0]; d[1] = e[1]; d[2] = e[2]; d[3] = 255;
        }
}

}  // namespace

// The native strip [lo | lo+1 | lo+2 | lo+3] a pan windows across, lo =
// min(old, new) - 1.  Edge slots resolve like the steady frame
// (widescreen_neighbors): real screens, or an off-level fill.
std::vector<std::uint8_t> build_pan_strip(TransitionShellCtx& ctx, int old_s,
                                          int new_s,
                                          const FrameBuffer& new_center) {
    const int count = ctx.screen_count;
    const int lo = std::min(old_s, new_s) - 1;   // strip slot0 = lo
    std::vector<std::uint8_t> strip(
        static_cast<std::size_t>(kStripW) * kStripH * 4, 0);
    // The outgoing screen pans frozen, as last seen (the EXE never touches
    // the visible page during the scroll); other slots get the peek
    // treatment.
    const auto fill_real = [&](int slot, int s) {
        if (s == new_s) {
            put_slot(strip, slot, new_center);
            return;
        }
        FrameBuffer t{};
        ctx.compose_static(s, t, /*frozen_full=*/s == old_s);
        put_slot(strip, slot, t);
    };
    const bool fond_level =
        ctx.state->current_level == 1 || ctx.state->current_level == 5;
    fill_real(1, lo + 1);          // min   (always real)
    fill_real(2, lo + 2);          // min+1 (always real)
    // Edge slots (0 and 3) resolved like the steady frame
    // (widescreen_neighbors), so a warp seam or level edge is off-level during
    // the pan too.
    const bool secret = ctx.state->secret_flag != 0;
    const auto nb_l = presentation::widescreen_neighbors(
        ctx.state->current_level, lo + 1, secret, count);
    const auto nb_r = presentation::widescreen_neighbors(
        ctx.state->current_level, lo + 2, secret, count);
    const bool slot0_real = lo >= 0 && nb_l.left == lo;
    const bool slot3_real = lo + 3 < count && nb_r.right == lo + 3;
    // Left edge: FOND levels mirror ground + FOND sky; tile levels the steady
    // margin.
    if (slot0_real) {
        fill_real(0, lo);
    } else if (fond_level) {
        mirror_slot(ctx, strip, 0, lo + 1);
        fond_sky_band(ctx, strip, 0);
    } else {
        steady_margin_slot(ctx, strip, 0, lo + 1, -1);   // tile levels 3/7
    }
    // Right edge: L1's open-water end screen fills the slot with FOND + lake;
    // otherwise as the left edge.
    const bool l1_end_off_right = ctx.state->current_level == 1 &&
                                  lo + 2 == core::kLastScreen &&
                                  ctx.wsp->backdrop_ok();
    if (slot3_real) {
        fill_real(3, lo + 3);
    } else if (l1_end_off_right) {
        put_slot(strip, 3, ctx.wsp->backdrop());   // full FOND; water below
    } else if (fond_level) {
        mirror_slot(ctx, strip, 3, lo + 2);
        fond_sky_band(ctx, strip, 3);
    } else {
        steady_margin_slot(ctx, strip, 3, lo + 2, +1);   // tile levels 3/7
    }
    // L1 end screen: continue the real water into the off-level slot
    // (self-gating helper; guard that the last screen is in the strip).
    if (ctx.state->current_level == 1 && lo <= core::kLastScreen &&
        core::kLastScreen <= lo + 3)
        presentation::continue_l1_end_water(
            *ctx.state, *ctx.render,
            /*origin_x=*/(core::kLastScreen - lo) * 320,
            /*buf_w=*/kStripW, strip);
    // ── Seam-column continuity across the strip (tile_patterns) ──────
    pan_bridge_seams(ctx, strip, lo, new_s, new_center, slot0_real,
                     slot3_real);
    return strip;
}

void play_panorama_wide(TransitionShellCtx& ctx, int old_s, int new_s,
                        const FrameBuffer& new_center) {
    const int M = ctx.wsp->margin(), WN = ctx.wsp->native_w();
    const int H = kStripH;
    const int lo = std::min(old_s, new_s) - 1;
    const std::vector<std::uint8_t> strip =
        build_pan_strip(ctx, old_s, new_s, new_center);
    // The HD strip, upscaled band by band as the window reaches it: the
    // whole strip up front stalled the first frame on a handheld at 4x.
    enhance::LazyUpscaler hd_strip(strip, kStripW, H, ctx.hd_scale,
                                   *ctx.hd_profile);
    const int sWh = kStripW * ctx.hd_scale;   // HD strip row width
    const int wWh = WN * ctx.hd_scale, wHh = H * ctx.hd_scale;
    std::vector<std::uint8_t> work(
        static_cast<std::size_t>(wWh) * wHh * 4);   // HD window buffer
    const int x0_start = (old_s - lo) * 320 - M;   // window left edge, centred on OLD
    const int x0_end   = (new_s - lo) * 320 - M;   // …on NEW
    const int n = ctx.smooth_motion ? 36 : 12;     // SCROLL_FRAMES
    static int seq = 0;
    if (!*ctx.running) return;
    run_pan(ctx, n, [&](const PanStep& st) {
        const double t = std::min(1.0, st.p);
        const int x0 = std::clamp(
            static_cast<int>(std::lround(x0_start + (x0_end - x0_start) * t)),
            0, kStripW - WN);
        // OLDUVAI_DUMP_TRANSITION: the native strip window (ptrans_NNNN.bmp).
        if (std::getenv("OLDUVAI_DUMP_TRANSITION") != nullptr) {
            std::vector<std::uint8_t> nat(static_cast<std::size_t>(WN) * H * 4);
            for (int y = 0; y < H; ++y)
                std::copy_n(strip.begin() +
                                (static_cast<std::size_t>(y) * kStripW +
                                 static_cast<std::size_t>(x0)) * 4,
                            static_cast<std::size_t>(WN) * 4,
                            nat.begin() + static_cast<std::size_t>(y) * WN * 4);
            dump_frame("ptrans", -1, seq, nat.data(), WN, H);
        }
        const int x0h = x0 * ctx.hd_scale;   // HD strip x-offset
        hd_strip.ensure(x0, x0 + WN);
        for (int y = 0; y < wHh; ++y)
            std::copy_n(
                hd_strip.hd().begin() + (static_cast<std::size_t>(y) * sWh +
                                         static_cast<std::size_t>(x0h)) * 4,
                static_cast<std::size_t>(wWh) * 4,
                work.begin() + static_cast<std::size_t>(y) * wWh * 4);
        ctx.wsp->present_transition(work, /*with_hud=*/true,
                                    /*pre_upscaled=*/true);
    });
}

}  // namespace olduvai::presentation
