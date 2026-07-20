// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Blocking transition/cinematic players — extracted verbatim from
// run_platform_level (game_app.cpp) as OL-B3.  See transition_players.hpp
// for the extraction contract.  The bodies are a MOVE, not a rewrite: every
// loop, constant and comment matches the in-loop lambdas they replace.
#include "presentation/transition_players.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "core/constants.hpp"
#include "enhance/upscale.hpp"
#include "presentation/image_out.hpp"
#include "presentation/screens.hpp"
#include "presentation/tile_patterns.hpp"
#include "presentation/widescreen.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::presentation {

namespace {

// Metronome for the screen-transition / fade step pacing.  Keeps each
// step exactly step_ms apart by absorbing the time already spent on
// compose + present (incl. the vsync block when smooth-motion vsync is
// on) — so a transition's wall-clock duration stays constant whether or
// not vsync is active (without it the vsync block would stack on the
// fixed SDL_Delay and run transitions ~1.5× slow).  Only ever delays
// LESS than the bare SDL_Delay(step_ms), so the no-vsync path is
// unchanged; a large gap (between transitions) self-resets.
// State lives on the ctx (pace_last), reset to 0 per ctx construction —
// identical to the old per-frame `Uint32 pace_last = 0;` local.
void paced(TransitionShellCtx& ctx, Uint32 step_ms) {
    const Uint32 now = SDL_GetTicks();
    if (ctx.pace_last != 0 && now - ctx.pace_last < step_ms)
        SDL_Delay(step_ms - (now - ctx.pace_last));
    ctx.pace_last = SDL_GetTicks();
}

}  // namespace

void play_transition(TransitionShellCtx& ctx, const FrameBuffer& oldf,
                     FrameBuffer& newf, int kind, char dir) {
    const bool smooth_t = ctx.smooth_motion;
    const Uint32 step_ms =
        smooth_t ? (1000 / 60) : ctx.frame_ms;
    auto pump = [&]() -> bool {
        SDL_Event e2;
        while (SDL_PollEvent(&e2)) {
            if (handle_fullscreen_toggle(e2, ctx.win)) continue;
            if (e2.type == SDL_QUIT) {
                *ctx.running = false;
                return false;
            }
        }
        return true;
    };
    // Debug aid: OLDUVAI_DUMP_TRANSITION=<dir> saves every
    // transition frame as a BMP (pre-upscale; dimensions match fb).
    const char* dump_dir = std::getenv("OLDUVAI_DUMP_TRANSITION");
    auto dump = [&](FrameBuffer& fr) {
        if (dump_dir == nullptr) return;
        static int seq = 0;
        char path[512];
        std::snprintf(path, sizeof path, "%s/trans_k%d_%04d.bmp",
                      dump_dir, kind, seq++);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
            fr.px.data(), fr.w, fr.h, 32, fr.w * 4,
            SDL_PIXELFORMAT_RGBA32);
        save_surface_image(s, path);
        SDL_FreeSurface(s);
    };
    // Work buffer: must match the source buffer dimensions (HD or native).
    FrameBuffer work{oldf.w, oldf.h};
    if (kind == 2) {   // fade out the old, fade in the new
        const int n = smooth_t ? 36 : kFadeFrames;
        for (int f2 = 0; f2 <= n; ++f2) {
            apply_fade(work, oldf,
                       static_cast<double>(f2) / n);
            ctx.upload_and_show(work);
            dump(work);
            paced(ctx, step_ms);
            if (!pump()) return;
        }
        for (int f2 = n; f2 >= 0; --f2) {
            apply_fade(work, newf,
                       static_cast<double>(f2) / n);
            ctx.upload_and_show(work);
            dump(work);
            paced(ctx, step_ms);
            if (!pump()) return;
        }
        return;
    }
    // kind 3: enhanced secret-entry slide (12 frames, downward pan).
    // kind 4: enhanced secret-exit slide (30 frames, upward pan + arc).
    // Both use the same blit_shifted pan-scroll core as kind 1 but with
    // a fixed frame count and an optional player overlay for kind 4.
    // Geometry (matches the reference slide transition exactly):
    //   'D' (down, entry): old UP off top, new FROM bottom.
    //   'U' (up, exit):   old DOWN off bottom, new FROM top.
    // blit_shifted is dimension-aware: uses dst.w/dst.h (== oldf.w/h in
    // HD), so pan distances scale with the buffer size.
    const bool is_slide = (kind == 3 || kind == 4);
    const int buf_w_t = oldf.w;   // transition buffer width  (HD or 320)
    const int buf_h_t = oldf.h;   // transition buffer height (HD or 200)
    auto blit_shifted = [buf_w_t, buf_h_t](FrameBuffer& dst,
                                            const FrameBuffer& src,
                                            int sdx, int sdy) {
        for (int y2 = 0; y2 < buf_h_t; ++y2) {
            const int sy = y2 - sdy;
            if (sy < 0 || sy >= buf_h_t) continue;
            const int x0 = sdx > 0 ? sdx : 0;
            const int x1 = buf_w_t + (sdx < 0 ? sdx : 0);
            if (x0 >= x1) continue;
            std::copy_n(
                src.px.begin() +
                    (static_cast<std::size_t>(sy) * buf_w_t +
                     static_cast<std::size_t>(x0 - sdx)) * 4,
                static_cast<std::size_t>(x1 - x0) * 4,
                dst.px.begin() +
                    (static_cast<std::size_t>(y2) * buf_w_t +
                     static_cast<std::size_t>(x0)) * 4);
        }
    };
    const int n = is_slide ? (kind == 3 ? 12 : 30)
                           : (smooth_t ? 36 : 12);   // SCROLL_FRAMES
    // Arc parameters for kind 4 (exit slide + player overlay).
    // Matches the reference: LINEAR X lerp + parabolic Y (both
    // engines are linear-X; the "velocity-aware quadratic" both this
    // file and the Python catalog once described was never shipped).
    const int arc_sx = ctx.slide_secret_exit_x;   // for facing only
    const int arc_ex = ctx.slide_end_x;
    const int arc_ey = ctx.slide_end_y;
    // Jump sprite facing from net travel; on exact tie (ex == sx)
    // preserve the player's existing facing (matches the reference).
    const bool arc_flip = (arc_ex < arc_sx)
                          || (arc_ex == arc_sx && ctx.state->player.facing_left);
    const auto& arc_spr_mat = ctx.render->entity_sprites;
    const auto& arc_pal = ctx.render->palette;
    // Enhanced secret-exit overlay (kind 4), two phases:
    //  • PAN (f2<=n): the player is "baked into" the incoming surface at
    //    its bottom (bake_y), so it rides IN with the surface roll rather
    //    than floating free — screen y = bake_y + ndy (the 'U' pan's
    //    native ndy = (t-1)*200).  No desync, no fixed-screen pop: the
    //    sprite is part of the surface that's scrolling in.
    //  • ARC (f2>n): pan done, surface static — the player arcs from the
    //    bake point up to the surface resume (arc_ex, arc_ey), landing
    //    exactly where gameplay continues.
    // Jump pose throughout.  Pure render overlay in the no-gameplay
    // window; --enhanced only.
    const int n_arc = (kind == 4) ? 26 : 0;   // arc-landing frames
    const int bake_y = 185;                   // bottom of the surface —
                                              // where the player is baked
                                              // in and rides the roll-in
    constexpr double kArcPeak = 30.0;         // arc-landing apex height
    for (int f2 = 1; f2 <= n + n_arc; ++f2) {
        const double t = std::min(1.0, static_cast<double>(f2) / n);
        int odx = 0, ody = 0, ndx = 0, ndy = 0;
        if (is_slide) {
            // Secret entry ('D'): old UP, new from bottom.
            // Secret exit ('U'):  old DOWN, new from top.
            // Pan distance = full buffer height (HD or 200).
            if (kind == 3) {   // 'D'
                ody = -static_cast<int>(t * buf_h_t);
                ndy = buf_h_t + ody;
            } else {           // 'U'
                ody = static_cast<int>(t * buf_h_t);
                ndy = ody - buf_h_t;
            }
        } else {
            // Surface pan: distance = full buffer width/height.
            switch (dir) {
                case 'R':
                    odx = -static_cast<int>(t * buf_w_t);
                    ndx = buf_w_t + odx;
                    break;
                case 'L':
                    odx = static_cast<int>(t * buf_w_t);
                    ndx = odx - buf_w_t;
                    break;
                case 'D':
                    ody = -static_cast<int>(t * buf_h_t);
                    ndy = buf_h_t + ody;
                    break;
                default:   // 'U'
                    ody = static_cast<int>(t * buf_h_t);
                    ndy = ody - buf_h_t;
                    break;
            }
        }
        std::fill(work.px.begin(), work.px.end(), 0);
        for (std::size_t i = 3; i < work.px.size(); i += 4) {
            work.px[i] = 255;
        }
        blit_shifted(work, oldf, odx, ody);
        blit_shifted(work, newf, ndx, ndy);
        // kind 4: draw the player jump-arc overlay.
        // x(t) = sx + (ex-sx)*t                       (linear X lerp)
        // y(t) = lerp(sy,ey,t) - peak * 4*t*(1-t)    (parabolic arc, peak at t=0.5)
        // Matches the reference and spec §F7 exit formula.
        // blit_sprite(FrameBuffer&,...) now uses work.w/work.h so the
        // sprite blits at the right position in an HD work buffer.  At
        // scale 1 it writes at native coords (unchanged classic behaviour).
        if (kind == 4) {
            constexpr int spr2 = systems::kSprPlayerJump;
            int px2, py2;
            if (f2 <= n) {
                // PAN — baked into the surface at its bottom: ride in with
                // the surface roll ('U' pan native ndy = (t-1)*200).  The
                // sprite stays glued to the surface so it never pops to a
                // fixed screen spot.
                px2 = arc_sx;
                py2 = bake_y +
                      static_cast<int>(std::lround((t - 1.0) * 200.0));
            } else {
                // ARC — surface static: land from the bake point to the
                // surface resume (arc_ex, arc_ey), no end snap.
                const double ta =
                    static_cast<double>(f2 - n) / static_cast<double>(n_arc);
                px2 = static_cast<int>(std::lround(
                    arc_sx + (arc_ex - arc_sx) * ta));
                const double lin = bake_y + (arc_ey - bake_y) * ta;
                py2 = static_cast<int>(std::lround(
                    lin - kArcPeak * 4.0 * ta * (1.0 - ta)));
            }
            // Harness: trace the OVERLAY's drawn position vs the player's
            // real resume position (arc_ex,arc_ey).  |overlay[f2=1] -
            // resume| is the start-of-slide pop; overlay[last] must equal
            // resume (no end snap).  Native coords (px2/py2 pre-scale).
            if (ctx.draw_log != nullptr)
                std::fprintf(ctx.draw_log,
                             "{\"trans\":%d,\"f2\":%d,\"of\":%d,"
                             "\"px\":%d,\"py\":%d,\"resx\":%d,\"resy\":%d,"
                             "\"exitx\":%d}\n",
                             kind, f2, n + n_arc, px2, py2, arc_ex, arc_ey,
                             arc_sx);
            if (spr2 < static_cast<int>(arc_spr_mat.size())) {
                if (ctx.hd) {
                    auto wrt = ctx.make_rt(work);
                    blit_sprite(wrt, arc_spr_mat[spr2], arc_pal,
                                px2, py2, arc_flip);
                } else {
                    blit_sprite(work, arc_spr_mat[spr2], arc_pal,
                                px2, py2, arc_flip);
                }
            }
        }
        ctx.upload_and_show(work);
        dump(work);
        paced(ctx, step_ms);
        if (!pump()) return;
    }
}

// ── Widescreen transition playback (§8.7 wide transitions) ──────────
// Mirror play_transition's kind-1 pan and kind-2 fade, but over WIDE
// native buffers (ws_native_w × 200) presented through the wide texture
// via present_wide_transition — so width AND HUD position are continuous
// with the steady widescreen frame (no 320 pillarbox pop, no HUD jump).
// The pan slides the WHOLE wide view; the fade blends the wide buffers.
// oldw / neww are pre-wrapped wide buffers (peek or bezel per side).
void play_transition_wide(TransitionShellCtx& ctx,
                          std::vector<std::uint8_t>& oldw,
                          std::vector<std::uint8_t>& neww, int kind, char dir) {
    const bool smooth_t = ctx.smooth_motion;
    const Uint32 step_ms = smooth_t ? (1000 / 60) : ctx.frame_ms;
    const int W = ctx.ws_native_w, H = 200;
    const int Wh = W * ctx.hd_scale, Hh = H * ctx.hd_scale;
    // Upscale the two STATIC wide buffers ONCE; every kind below shifts/
    // fades the HD buffers and presents pre_upscaled — no per-frame
    // upscale (was the choppy secret/cave transitions; mirrors the
    // kind-1 panorama + steady-state static-bg cache, task #61).
    std::vector<std::uint8_t> hd_old =
        enhance::upscale_rgba(oldw, W, H, ctx.hd_scale, *ctx.hd_profile);
    std::vector<std::uint8_t> hd_new =
        enhance::upscale_rgba(neww, W, H, ctx.hd_scale, *ctx.hd_profile);
    auto pump = [&]() -> bool {
        SDL_Event e2;
        while (SDL_PollEvent(&e2)) {
            if (handle_fullscreen_toggle(e2, ctx.win)) continue;
            if (e2.type == SDL_QUIT) { *ctx.running = false; return false; }
        }
        return true;
    };
    const char* dump_dir = std::getenv("OLDUVAI_DUMP_TRANSITION");
    auto dump = [&](std::vector<std::uint8_t>& fr) {
        if (dump_dir == nullptr) return;
        static int wseq = 0;
        char path[512];
        std::snprintf(path, sizeof path, "%s/wtrans_k%d_%04d.bmp",
                      dump_dir, kind, wseq++);
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
            fr.data(), Wh, Hh, 32, Wh * 4, SDL_PIXELFORMAT_RGBA32);
        save_surface_image(s, path);
        SDL_FreeSurface(s);
    };
    std::vector<std::uint8_t> work(
        static_cast<std::size_t>(Wh) * Hh * 4);   // HD work buffer
    // HD shift: caller passes NATIVE sdx/sdy (the kind math stays in
    // native units); scaled by hd_scale here so it operates on the
    // pre-upscaled HD buffers.
    auto blit_shifted_w = [&](std::vector<std::uint8_t>& dst,
                              const std::vector<std::uint8_t>& src,
                              int sdx, int sdy) {
        const int sdxh = sdx * ctx.hd_scale, sdyh = sdy * ctx.hd_scale;
        for (int y2 = 0; y2 < Hh; ++y2) {
            const int sy = y2 - sdyh;
            if (sy < 0 || sy >= Hh) continue;
            const int x0 = sdxh > 0 ? sdxh : 0;
            const int x1 = Wh + (sdxh < 0 ? sdxh : 0);
            if (x0 >= x1) continue;
            std::copy_n(
                src.begin() +
                    (static_cast<std::size_t>(sy) * Wh +
                     static_cast<std::size_t>(x0 - sdxh)) * 4,
                static_cast<std::size_t>(x1 - x0) * 4,
                dst.begin() +
                    (static_cast<std::size_t>(y2) * Wh +
                     static_cast<std::size_t>(x0)) * 4);
        }
    };
    if (kind == 2) {   // fade out old, fade in new (wide)
        const int n = smooth_t ? 36 : kFadeFrames;
        // apply_fade is dimension-agnostic; wrap the wide vectors as
        // FrameBuffers so it operates on the wide pixels directly.
        FrameBuffer wf{Wh, Hh}, of{Wh, Hh}, nf{Wh, Hh};
        of.px = hd_old; nf.px = hd_new;
        for (int f2 = 0; f2 <= n; ++f2) {
            apply_fade(wf, of, static_cast<double>(f2) / n);
            ctx.present_wide_transition(wf.px, /*with_hud=*/true,
                                        /*pre_upscaled=*/true);
            dump(wf.px);
            paced(ctx, step_ms);
            if (!pump()) return;
        }
        for (int f2 = n; f2 >= 0; --f2) {
            apply_fade(wf, nf, static_cast<double>(f2) / n);
            ctx.present_wide_transition(wf.px, /*with_hud=*/true,
                                        /*pre_upscaled=*/true);
            dump(wf.px);
            paced(ctx, step_ms);
            if (!pump()) return;
        }
        return;
    }
    if (kind == 3 || kind == 4) {
        // Enhanced secret slides over the WIDE view — same geometry + arc
        // as play_transition's kind 3/4 (vertical pan, fixed frame counts,
        // kind-4 player jump arc), but the buffers are ws_native_w wide and
        // presented via present_wide_transition; the arc sprite blits at
        // +ws_margin so it lands at the centre-320 position.  No 320 bars.
        const int ns = (kind == 3) ? 12 : 30;
        const int n_arc = (kind == 4) ? 26 : 0;
        const int bake_y = 185;
        constexpr double kArcPeak = 30.0;
        const int arc_sx = ctx.slide_secret_exit_x;
        const int arc_ex = ctx.slide_end_x;
        const int arc_ey = ctx.slide_end_y;
        const bool arc_flip =
            (arc_ex < arc_sx) ||
            (arc_ex == arc_sx && ctx.state->player.facing_left);
        const auto& arc_spr_mat = ctx.render->entity_sprites;
        const auto& arc_pal = ctx.render->palette;
        for (int f2 = 1; f2 <= ns + n_arc; ++f2) {
            const double t = std::min(1.0, static_cast<double>(f2) / ns);
            int ody = 0, ndy = 0;
            if (kind == 3) { ody = -static_cast<int>(t * H); ndy = H + ody; }
            else           { ody =  static_cast<int>(t * H); ndy = ody - H; }
            std::fill(work.begin(), work.end(), 0);
            for (std::size_t i = 3; i < work.size(); i += 4) work[i] = 255;
            blit_shifted_w(work, hd_old, 0, ody);
            blit_shifted_w(work, hd_new, 0, ndy);
            if (kind == 4) {
                constexpr int spr2 = systems::kSprPlayerJump;
                int px2, py2;
                if (f2 <= ns) {
                    px2 = arc_sx;
                    py2 = bake_y +
                          static_cast<int>(std::lround((t - 1.0) * 200.0));
                } else {
                    const double ta = static_cast<double>(f2 - ns) /
                                      static_cast<double>(n_arc);
                    px2 = static_cast<int>(std::lround(
                        arc_sx + (arc_ex - arc_sx) * ta));
                    const double lin = bake_y + (arc_ey - bake_y) * ta;
                    py2 = static_cast<int>(std::lround(
                        lin - kArcPeak * 4.0 * ta * (1.0 - ta)));
                }
                if (spr2 < static_cast<int>(arc_spr_mat.size())) {
                    RenderTarget wrt{work.data(), Wh, Hh, ctx.hd_scale,
                                     ctx.hd_cache, ctx.hd_profile};
                    wrt.origin_x = ctx.ws_margin;
                    blit_sprite(wrt, arc_spr_mat[spr2], arc_pal,
                                px2, py2, arc_flip);
                }
            }
            ctx.present_wide_transition(work, /*with_hud=*/true,
                                        /*pre_upscaled=*/true);
            dump(work);
            paced(ctx, step_ms);
            if (!pump()) return;
        }
        return;
    }
    // kind 1: surface pan-scroll over the WHOLE wide view.
    const int n = smooth_t ? 36 : 12;   // SCROLL_FRAMES
    for (int f2 = 1; f2 <= n; ++f2) {
        const double t = std::min(1.0, static_cast<double>(f2) / n);
        int odx = 0, ody = 0, ndx = 0, ndy = 0;
        switch (dir) {
            case 'R': odx = -static_cast<int>(t * W); ndx = W + odx; break;
            case 'L': odx =  static_cast<int>(t * W); ndx = odx - W; break;
            case 'D': ody = -static_cast<int>(t * H); ndy = H + ody; break;
            default:  ody =  static_cast<int>(t * H); ndy = ody - H; break;
        }
        std::fill(work.begin(), work.end(), 0);
        for (std::size_t i = 3; i < work.size(); i += 4) work[i] = 255;
        blit_shifted_w(work, hd_old, odx, ody);
        blit_shifted_w(work, hd_new, ndx, ndy);
        ctx.present_wide_transition(work, /*with_hud=*/true,
                                    /*pre_upscaled=*/true);
        dump(work);
        paced(ctx, step_ms);
        if (!pump()) return;
    }
}

// ── Widescreen PANORAMA pan (kind-1 surface scroll) ──────────────────
// Slide a (320+2M) window across a CONTINUOUS native strip of the four
// screens involved [min-1 | min | min+1 | min+2], advancing by exactly
// 320 (one screen).  The old path slid two independently margin-composed
// wide buffers ([peek|center|peek]) by the FULL wide width 320+2M, which
// over-scrolled by 2M at the moving seam → the shared FOND backdrop +
// terrain jumped ("tear", task: ws transition tearing).  Here the
// margins ARE the real adjacent screens at every instant, so everything
// flows continuously.  The incoming screen carries the player
// (new_center); off-level edge slots (level first/last, folds in #60)
// clamp the adjacent screen's near edge column — a tear-free
// continuation of its sky+ground bands.
void play_panorama_wide(TransitionShellCtx& ctx, int old_s, int new_s,
                        const FrameBuffer& new_center) {
    const bool smooth_t = ctx.smooth_motion;
    const Uint32 step_ms = smooth_t ? (1000 / 60) : ctx.frame_ms;
    const int M = ctx.ws_margin, WN = ctx.ws_native_w, H = 200;
    const int count = ctx.screen_count;
    const int lo = std::min(old_s, new_s) - 1;   // strip slot0 = lo
    constexpr int kStripW = 4 * 320;
    std::vector<std::uint8_t> strip(
        static_cast<std::size_t>(kStripW) * H * 4, 0);
    auto put_slot = [&](int i, const FrameBuffer& src) {
        for (int y = 0; y < H; ++y)
            std::copy_n(
                src.px.begin() + static_cast<std::size_t>(y) * 320 * 4,
                320 * 4,
                strip.begin() + (static_cast<std::size_t>(y) * kStripW +
                                 static_cast<std::size_t>(i) * 320) * 4);
    };
    auto fill_real = [&](int i, int s) {
        if (s == new_s) { put_slot(i, new_center); return; }
        FrameBuffer t{};
        // The OUTGOING screen pans FROZEN, sprites as last seen —
        // the EXE never touches the visible page during the scroll
        // (transition_pan_content_frozen_sprites.md); the store
        // holds its exact last live state post-rebind.  Other slots
        // keep the peek treatment (spawn-post enemies + statics).
        ctx.compose_static(s, t, /*frozen_full=*/s == old_s);
        put_slot(i, t);
    };
    // Overwrite an off-level slot's SKY band (rows above the ground band)
    // with the real FOND backdrop.  clamp_slot's single-column smear
    // streaks the distant sky/mountains during the pan; the steady margin
    // samples the backdrop there (compose_widescreen bg_extend), so
    // copying the FOND reproduces it.  The floor in the ground band stays
    // from clamp_slot.  For shared-FOND surface levels only (internal 1
    // jungle / 5 icy); 3/7 are tile-based with no FOND (still smeared —
    // separate follow-up).
    const bool fond_level =
        ctx.state->current_level == 1 || ctx.state->current_level == 5;
    // Off-level slot fill that MIRRORS the adjacent screen's edge strip
    // (slot col x ← adj-screen col 319-x) instead of clamp_slot's single-
    // column smear — so a textured edge (icy water, dark-woods forest)
    // reads as a real reflected strip, not a smudged column.  Mirrors the
    // whole slot; FOND levels overwrite the sky band afterwards via
    // fond_sky_band, non-FOND levels (dark woods) keep the mirror.
    auto offlevel_slot = [&](int slot, int adj_s) {
        FrameBuffer t{};
        ctx.compose_static(adj_s, t, /*frozen_full=*/false);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < 320; ++x) {
                const int sc = 319 - x;   // reflect across the screen edge
                const std::uint8_t* e =
                    &t.px[(static_cast<std::size_t>(y) * 320 + sc) * 4];
                std::uint8_t* d =
                    &strip[(static_cast<std::size_t>(y) * kStripW +
                            static_cast<std::size_t>(slot) * 320 + x) * 4];
                d[0] = e[0]; d[1] = e[1]; d[2] = e[2]; d[3] = 255;
            }
    };
    auto fond_sky_band = [&](int slot) {
        if (!ctx.ws_backdrop_ok) return;
        const int gb = H - presentation::kWideGroundBandRows;
        for (int y = 0; y < gb; ++y)
            std::memcpy(&strip[(static_cast<std::size_t>(y) * kStripW +
                                static_cast<std::size_t>(slot) * 320) * 4],
                        &ctx.ws_backdrop->px[static_cast<std::size_t>(y) * 320 * 4],
                        320 * 4);
    };
    // Off-level edge fill that is PIXEL-IDENTICAL to the steady margin:
    // compose the adjacent screen's FULL wide static bg exactly the way
    // the static view does (compose_surface_screen_wide_native →
    // compose_static_wide_bg_native: torus sky + mirror ground + the
    // re-drawn bg-tile rows the mirror/clamp fills can't reproduce) and
    // copy its OUTER margin into the slot's inner M columns — the only
    // part the window ever shows.  `side` < 0 = left slot (its RIGHT M
    // cols ← the screen's LEFT margin); > 0 = right slot (its LEFT M cols
    // ← the RIGHT margin).  This is what kills the 1-frame pop at the
    // pan↔steady hand-off (the earlier mirror differed in the sky band
    // and lacked the forest-backdrop / floor row extension).
    auto steady_margin_slot = [&](int slot, int adj_s, int side) {
        const presentation::FrameBuffer* bd =
            (ctx.ws_backdrop_ok && ctx.render->visual_background)
                ? ctx.ws_backdrop
                : nullptr;
        std::vector<std::uint8_t> wide;
        ctx.compose_wide_native(adj_s, M, bd, wide);
        const int wide_w = 320 + 2 * M;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < M; ++x) {
                const int sc = (side < 0) ? x : (M + 320 + x);
                const int dc = (side < 0) ? (320 - M + x) : x;
                const std::uint8_t* e =
                    &wide[(static_cast<std::size_t>(y) * wide_w + sc) * 4];
                std::uint8_t* d =
                    &strip[(static_cast<std::size_t>(y) * kStripW +
                            static_cast<std::size_t>(slot) * 320 + dc) * 4];
                d[0] = e[0]; d[1] = e[1]; d[2] = e[2]; d[3] = 255;
            }
    };
    fill_real(1, lo + 1);          // min   (always real)
    fill_real(2, lo + 2);          // min+1 (always real)
    // Decide the EDGE slots (0 = left of the strip, 3 = right) the SAME
    // way the steady frame does — via widescreen_neighbors — so a screen
    // reached/left by warp (Dark Woods trunk: s9 right, s12 left) or a
    // level edge resolves to OFF-LEVEL during the pan exactly as it does
    // when static.  Without this the pan kept showing the trunk (real
    // index neighbour) and smearing the no-neighbour edge mid-slide.
    const bool secret = ctx.state->secret_flag != 0;
    const auto nb_l = presentation::widescreen_neighbors(
        ctx.state->current_level, lo + 1, secret, count);
    const auto nb_r = presentation::widescreen_neighbors(
        ctx.state->current_level, lo + 2, secret, count);
    const bool slot0_real = lo >= 0 && nb_l.left == lo;
    const bool slot3_real = lo + 3 < count && nb_r.right == lo + 3;
    // LEFT edge.  FOND levels: mirror ground + real FOND sky.  Dark Woods
    // (internal 3, no FOND): mirror the whole forest strip.  L7: smear.
    if (slot0_real) {
        fill_real(0, lo);
    } else if (fond_level) {
        offlevel_slot(0, lo + 1);
        fond_sky_band(0);
    } else {
        // Tile levels (3/7): the exact steady margin — the L7 warp
        // seams (S13 left of a 13->14 pan) smeared cave-hall pixels
        // with clamp_slot; the steady margin is the validated wall/
        // row fill the pan hands off to.
        steady_margin_slot(0, lo + 1, -1);
    }
    // RIGHT edge.  L1's open-water end screen fills the whole slot with
    // the FOND + lake; else mirror (FOND/dark-woods) or smear (L7).
    const bool l1_end_off_right = ctx.state->current_level == 1 &&
                                  lo + 2 == core::kLastScreen &&
                                  ctx.ws_backdrop_ok;
    if (slot3_real) {
        fill_real(3, lo + 3);
    } else if (l1_end_off_right) {
        put_slot(3, *ctx.ws_backdrop);             // full FOND; water added below
    } else if (fond_level) {
        offlevel_slot(3, lo + 2);
        fond_sky_band(3);
    } else {
        // Tile levels (3/7): exact steady margin (S9 right of an 8->9
        // pan showed a clamp smear where steady has the gray wall).
        steady_margin_slot(3, lo + 2, +1);
    }
    // L1 mid-air-island END screen (sprite 7 = water): the off-level slot
    // to the right of the last screen was just edge-clamped (col 319
    // smeared), losing the lake.  Continue the REAL water past the island
    // into the strip so the 17→18 pan shows the same water as the steady
    // frame instead of a smeared/borked edge.  Self-gating in the helper
    // (no-op off L1's last screen); guard here that screen 18 is in-strip.
    if (ctx.state->current_level == 1 && lo <= core::kLastScreen &&
        core::kLastScreen <= lo + 3)
        presentation::continue_l1_end_water(
            *ctx.state, *ctx.render,
            /*origin_x=*/(core::kLastScreen - lo) * 320,
            /*buf_w=*/kStripW, strip);
    // ── Seam-column continuity across the strip (tile_patterns) ──────
    // Same laws as the steady wide compose: a trunk/pillar straddling a
    // screen edge must not cut at a slot boundary mid-pan (the
    // S12→S13→S14 transient vertical cut).  For every REAL slot, blit
    // its screen's straddling columns so the overhang crosses into the
    // adjacent slot — clipped away from its OWN slot (the in-slot part
    // is already drawn with the authored z-order).
    //
    // CAUTION — the strip slots are FULL composes with ENTITIES (and
    // the new slot carries the BAKED PLAYER), unlike the steady static
    // bg.  A whole-slot "authored tiles win" redraw buried the player
    // under the 144-wide bark tiles (user repro: S12→S13 walk, player
    // vanished for the whole pan).  So the level-tile redraw is
    // restricted to the exact BANDS an overhang actually landed on
    // (≤ ~32 px past a boundary), and the player's own box is restored
    // from new_center LAST so the player always wins in his region.
    {
        std::vector<std::pair<int, int>> real_slots = {{1, lo + 1},
                                                       {2, lo + 2}};
        if (slot0_real) real_slots.push_back({0, lo});
        if (slot3_real) real_slots.push_back({3, lo + 3});
        std::vector<std::pair<int, presentation::LevelRenderAssets>>
            slot_assets;
        std::vector<std::pair<int, int>> bands;   // strip-x [lo, hi)
        presentation::RenderTarget srt{strip.data(), kStripW, H, 1,
                                       nullptr, nullptr};
        for (const auto& slot_scr : real_slots) {
            // Named locals, not a structured binding, so the spill lambda below
            // can capture `slot` under C++17 (capturing a structured binding is
            // a C++20 extension).
            const int slot = slot_scr.first;
            const int scr = slot_scr.second;
            presentation::LevelRenderAssets ra;
            systems::SystemsState sst;
            ctx.build_assets(scr, ra, sst);
            auto spill = [&](bool right_edge) {
                srt.origin_x = slot * 320;
                srt.clip_x_lo =
                    right_edge ? (slot + 1) * 320 : -(1 << 28);
                srt.clip_x_hi =
                    right_edge ? (1 << 28) : slot * 320;
                int ext_lo = 1 << 28, ext_hi = -(1 << 28);
                for (const auto& tp :
                     presentation::tile_patterns::seam_straddling_tiles(
                         ra.tiles, ra.tile_sprites, right_edge)) {
                    if (tp.sprite_idx < 0 ||
                        tp.sprite_idx >=
                            static_cast<int>(ra.tile_sprites.size()))
                        continue;
                    const auto& spr =
                        ra.tile_sprites[static_cast<std::size_t>(
                            tp.sprite_idx)];
                    presentation::blit_sprite(srt, spr, ra.palette,
                                              tp.x, tp.y);
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
        // Authored seam holes bridged across adjacent REAL slots —
        // the same tile_patterns::seam_row_bridges the steady view
        // uses (the L7 S1|S2 jumppad rail), so a hole doesn't
        // reappear for the duration of the pan.  Bridge blits join
        // the bands, so the authored-redraw + player-box passes
        // below apply to them too.
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
        // The player rides the incoming slot BAKED into new_center —
        // restore his box from it last, so neither an overhang nor the
        // band redraw can cover him (compose order: player above all).
        if (!bands.empty()) {
            const int pslot = new_s - lo;
            const int bx0 = std::max(0, ctx.state->player.x - 16);
            const int bx1 = std::min(320, ctx.state->player.x + 48);
            const int by0 = std::max(0, ctx.state->player.y - 24);
            const int by1 = std::min(H, ctx.state->player.y + 48);
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
    // Upscale the static strip ONCE (omniscale included) — the pan then
    // windows the pre-upscaled HD strip per frame with NO per-frame
    // upscale (was the choppiness; mirrors the steady-state cache #61).
    std::vector<std::uint8_t> hd_strip =
        enhance::upscale_rgba(strip, kStripW, H, ctx.hd_scale,
                              *ctx.hd_profile);
    const int sWh = kStripW * ctx.hd_scale;   // HD strip row width
    const int wWh = WN * ctx.hd_scale, wHh = H * ctx.hd_scale;
    std::vector<std::uint8_t> work(
        static_cast<std::size_t>(wWh) * wHh * 4);   // HD window buffer
    const int x0_start = (old_s - lo) * 320 - M;   // window left edge, centred on OLD
    const int x0_end   = (new_s - lo) * 320 - M;   // …on NEW
    const int n = smooth_t ? 36 : 12;              // SCROLL_FRAMES
    // Debug aid: OLDUVAI_DUMP_TRANSITION covers this path too (the
    // legacy slide + non-wide play_transition already dump) — the
    // panorama saves the NATIVE strip window per frame as
    // ptrans_NNNN.bmp so pan seams can be verified headlessly.
    const char* pan_dump = std::getenv("OLDUVAI_DUMP_TRANSITION");
    for (int f2 = 1; f2 <= n && *ctx.running; ++f2) {
        const double t = std::min(1.0, static_cast<double>(f2) / n);
        int x0 = static_cast<int>(
            std::lround(x0_start + (x0_end - x0_start) * t));
        if (x0 < 0) x0 = 0;
        if (x0 > kStripW - WN) x0 = kStripW - WN;
        if (pan_dump != nullptr) {
            static int pseq = 0;
            std::vector<std::uint8_t> nat(
                static_cast<std::size_t>(WN) * H * 4);
            for (int y = 0; y < H; ++y)
                std::copy_n(strip.begin() +
                                (static_cast<std::size_t>(y) * kStripW +
                                 static_cast<std::size_t>(x0)) * 4,
                            static_cast<std::size_t>(WN) * 4,
                            nat.begin() +
                                static_cast<std::size_t>(y) * WN * 4);
            char path[512];
            std::snprintf(path, sizeof path, "%s/ptrans_%04d.bmp",
                          pan_dump, pseq++);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
                nat.data(), WN, H, 32, WN * 4, SDL_PIXELFORMAT_RGBA32);
            save_surface_image(s, path);
            SDL_FreeSurface(s);
        }
        const int x0h = x0 * ctx.hd_scale;         // HD strip x-offset
        for (int y = 0; y < wHh; ++y)
            std::copy_n(
                hd_strip.begin() + (static_cast<std::size_t>(y) * sWh +
                                    static_cast<std::size_t>(x0h)) * 4,
                static_cast<std::size_t>(wWh) * 4,
                work.begin() + static_cast<std::size_t>(y) * wWh * 4);
        ctx.present_wide_transition(work, /*with_hud=*/true,
                                    /*pre_upscaled=*/true);
        paced(ctx, step_ms);
        SDL_Event e2;
        while (SDL_PollEvent(&e2)) {
            if (handle_fullscreen_toggle(e2, ctx.win)) continue;
            if (e2.type == SDL_QUIT) *ctx.running = false;
        }
    }
}

}  // namespace olduvai::presentation
