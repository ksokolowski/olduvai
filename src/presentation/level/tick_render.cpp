// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/level/tick_render.hpp"

#include <vector>

#include "presentation/diag/draw_log.hpp"
#include "presentation/diag/level_diag.hpp"
#include "presentation/game_app.hpp"                 // GameOptions
#include "presentation/image_out.hpp"
#include "presentation/level/level_fx.hpp"
#include "presentation/level/level_state.hpp"
#include "presentation/render/banners.hpp"
#include "presentation/render/frame_presenter.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/render/lerp_snapshot.hpp"
#include "presentation/render/level_surface.hpp"
#include "presentation/render/widescreen_presenter.hpp"
#include "presentation/sequence/screen_change.hpp"   // PrevFrame
#include "systems/frame_runner.hpp"

namespace olduvai::presentation {

namespace {

// Smooth motion runs in live play only: --play-frames and --play-shot want
// one deterministic frame per tick.
bool smooth_motion_on(const GameOptions& opts) {
    return opts.enhance.smooth_motion && opts.frames <= 0 &&
           opts.screenshot.empty();
}

// A screen or cave/secret mode change is a teleport even when the hop is
// under 16 px (L3 S4 cave entry: 9,11 px): every lerp snaps this tick.
bool is_warp_tick(const PrevFrame& pf, const systems::SystemsState& st) {
    const bool inside = st.cave_flag != 0 || st.secret_flag != 0;
    return st.current_screen != pf.screen || inside != pf.inside;
}

}  // namespace

TickRender::TickRender(LevelSurface& surface, const LevelViewDeps& deps,
                       FrameBuffer& fb, WidescreenPresenter& wsp,
                       FramePresenter& fp, SmoothPacer pacer)
    : g_(deps.g), surface_(surface), fb_(fb), fx_(deps.fx), wsp_(wsp), fp_(fp),
      diag_(deps.diag), opts_(deps.opts), hd_scale_(surface.hd_scale()),
      pacer_(pacer) {}

void TickRender::compose(const Bubbles& bubbles) {
    auto rt = make_render_target(fb_, surface_, g_.hd_cache);
    blits_.clear();
    rt.blits = &blits_;   // for the steady present's diff
    compose_frame(rt, g_.state, g_.render, /*draw_player=*/true, bubbles);
    fx_.draw(rt, g_);
}

// Display only: the tick's one advance already ran on compose(), and a
// second would drop the club sprite (and its widescreen overflow) after the
// first sub-frame.
void TickRender::compose_sub(const Bubbles& bubbles) {
    auto rt = make_render_target(fb_, surface_, g_.hd_cache);
    blits_.clear();
    rt.blits = &blits_;
    rt.advance_state = false;
    rt.use_float_pos = true;   // 1-HD-px player/entities
    rt.player_fx = player_fx_;
    rt.player_fy = player_fy_;
    compose_frame(rt, g_.state, g_.render, /*draw_player=*/true, bubbles);
    fx_.draw(rt, g_);
}

void TickRender::advance_once(BannerPresenter& banners) {
    banners.arm_tick();   // the GET READY fly-away, on its rising edge
    if (fx_.l3_smoke_tail > 0) --fx_.l3_smoke_tail;
    // The teleport completes at the next tick's logic step, inside the
    // classifier's snapshot bracket, so cave->surface plays the fade pair.
    systems::tick_teleport_fx(g_.state);
    fp_.draw_hud_for(fb_);
    systems::tick_get_ready(g_.state);
}

// fb holds only blits over the screen's cached HD background: no secret
// room (its background is drawn live, bubbles between), no debug overlay, no
// bitmap GET READY (vector text off).
bool TickRender::steady_ok(const Bubbles& bubbles) const {
    return surface_.hd() && surface_.use_hd_text() &&
           fb_.w == 320 * hd_scale_ && !bubbles &&
           g_.state.secret_flag == 0 && !opts_.debug_collision &&
           !opts_.debug_entities && !opts_.debug_perf;
}

// The widescreen compose, or fb: only what changed when steady_ok.
void TickRender::present_frame(const Bubbles& bubbles) {
    if (wsp_.present_path()) {
        wsp_.present(bubbles);
        return;
    }
    maybe_dump_steady(fb_.px.data(), fb_.w, fb_.h);
    if (steady_ok(bubbles))
        fp_.present_steady(fb_, blits_,
                           static_bg_key(g_.state, g_.render, hd_scale_,
                                         *surface_.hd_profile()));
    else
        fp_.present(fb_);
}

bool TickRender::present(const PrevFrame& pf, const Bubbles& bubbles,
                         bool fluid_bubbles, int frame) {
    if (!smooth_motion_on(opts_)) {
        draw_debug_overlays(fb_, opts_, g_, diag_, hd_scale_);
        present_frame(bubbles);
        write_draw_log(diag_.draw_log.get(), frame, 0, g_.state);
        return false;
    }
    // The logic positions: the interpolation target, restored after the fill.
    const LogicPositions cur = save_logic_positions(g_.state);
    const bool warp_snap = is_warp_tick(pf, g_.state);

    // Fill the tick with interpolated frames (smooth_present.hpp: vsync at a
    // continuous alpha, else discrete_n even sub-frames).
    const bool vsync_ran = smooth_fill_tick(pacer_, [&](float alpha, int sub) {
        fx_.alpha = alpha;
        apply_interpolated(g_.state, cur, alpha, warp_snap, player_fx_,
                           player_fy_);
        std::vector<BubblePos> bubbles_logic;
        if (fluid_bubbles && g_.state.secret_flag) {
            bubbles_logic = lerp_fluid_bubbles(g_, alpha);
            if (diag_.hooks.bubble_trace)
                trace_slow_bubble(g_, frame, sub, hd_scale_);
        }
        compose_sub(bubbles);
        fp_.draw_hud_for(fb_);
        restore_fluid_bubbles(g_, bubbles_logic);
        draw_debug_overlays(fb_, opts_, g_, diag_, hd_scale_);

        // The widescreen overflow pass reads the float positions too.
        wsp_.set_float_pos(true, player_fx_, player_fy_);
        present_frame(bubbles);
        wsp_.set_float_pos(false);
        write_draw_log(diag_.draw_log.get(), frame, sub, g_.state);
        diag_.trace_pace(sub);
    });
    fx_.alpha = 1.0f;
    restore_logic_positions(g_.state, cur);
    return vsync_ran;
}

// HD / widescreen: the vector HUD text is in the output overlay, not fb, so
// read back the live present path's output before presenting (a
// post-present readback is black on Metal).  Classic: the bitmap HUD is in
// the 320x200 buffer.
void TickRender::capture_shot(const std::string& path, bool hd,
                              SDL_Renderer* ren, const Bubbles& bubbles) {
    if (!wsp_.active() && !hd) {
        save_rgba_image(fb_.px.data(), fb_.w, fb_.h, path);
        return;
    }
    if (wsp_.present_path())
        wsp_.present(bubbles, /*do_present=*/false);
    else
        fp_.present(fb_, /*with_hud=*/true, /*do_present=*/false);
    capture_renderer_output(ren, path);
}

}  // namespace olduvai::presentation
