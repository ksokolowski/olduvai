// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/sequence/screen_change.hpp"

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

#include "presentation/level/level_fx.hpp"      // make_bubble_hook
#include "presentation/level/level_setup.hpp"
#include "presentation/render/frame_presenter.hpp"
#include "presentation/render/widescreen_presenter.hpp"
#include "presentation/sequence/secret_slide.hpp"
#include "presentation/sequence/transition_players.hpp"
#include "systems/transitions.hpp"

namespace olduvai::presentation {

namespace {

// The outgoing frame the classified transition plays from, captured while
// the old screen is still bound.
void capture_outgoing(Loaded& g, TransitionState& trans, FramePresenter& fp,
                      const FrameBuffer& fb) {
    WidescreenPresenter& wsp = *fp.wsp;
    // Player-less: the pan's player rides the incoming screen; the slides
    // hide him (entry) or draw his arc (exit).
    const auto compose_old = [&](const std::function<void(RenderTarget&)>&
                                     bubbles) {
        {
            auto rt = make_render_target(trans.old_frame, *fp.surface,
                                         *fp.hd_cache);
            compose_frame(rt, g.state, g.render, /*draw_player=*/false,
                          bubbles);
        }
        fp.draw_hud_for(trans.old_frame);
    };
    // The slides' wide outgoing frame, wrapped with the old side's peek
    // cache, still live before the rebind.
    const auto compose_old_wide = [&](const std::function<void(RenderTarget&)>&
                                          bubbles) {
        if (!wsp.active()) return;
        FrameBuffer oc{};
        RenderTarget rt{oc.px.data(), 320, 200, 1, nullptr, nullptr};
        rt.advance_state = false;
        compose_frame(rt, g.state, g.render, /*draw_player=*/false, bubbles);
        wsp.wrap_wide(oc, trans.slide_old_wide);
        trans.slide_old_wide_ok = true;
    };
    switch (trans.kind) {
        case TransitionKind::kPan:
            compose_old({});
            break;
        case TransitionKind::kSecretEntry:
            compose_old({});
            compose_old_wide({});
            break;
        case TransitionKind::kSecretExit: {
            // Saved before bind_screen / clear_per_screen_state.  Uses the
            // last scatter in g.render.tiles: no new LCG draws (classic rolls
            // 0 here).
            trans.landing = slide_landing(g.state);
            const ShownAs as_left(g.state, room_as_left(g.state, trans.landing));
            // The bubbles as the last frame drew them (the room's own view:
            // the wide margins self-tile below).
            const auto bubbles = g.fluid_bubbles_initialized
                                     ? make_bubble_hook(g, /*ws_mirror=*/false)
                                     : std::function<void(RenderTarget&)>{};
            compose_old(bubbles);
            // The room's cache has no neighbours and no backdrop: its wide
            // margins self-tile.
            compose_old_wide(bubbles);
            break;
        }
        case TransitionKind::kFadePair:
            // The fade hides the player: the frame as displayed.
            trans.old_frame = fb;
            break;
        case TransitionKind::kNone:
            break;
    }
}

// The wide decision, for the post-rebind call that also needs the new screen.
struct WideOutgoing {
    bool old_wide;       // the OLD side is a ws_present_path screen
    bool eligible_kind;  // this transition kind can go wide at all
};

// The transition composed wide while the peek cache still holds the old screen.
WideOutgoing compose_outgoing_wide(Loaded& g, const PrevFrame& pf,
                                   TransitionState& trans,
                                   WidescreenPresenter& wsp) {
    // Widen the pan and the fade pair, whose 320 path pops the bars.  The
    // slides built their wide outgoing frame in capture_outgoing.
    trans.wide = false;
    trans.old_wide.clear();
    bool ws_old = false;
    const bool ws_eligible_kind = trans.kind != TransitionKind::kNone;
    if (wsp.active() &&
        (trans.kind == TransitionKind::kPan || trans.kind == TransitionKind::kFadePair)) {
        // The cache is the old screen's, but enter_cave/exit_cave already
        // flipped cave_flag/current_screen, which present_path() reads.  Gate
        // on pf.cave / pf.secret (the outgoing side), or a cave exit is wrapped
        // as a surface screen.
        ws_old = !pf.cave && !pf.secret && wsp.present_path();

        // Outgoing 320 centre from g.state (old screen, not yet rebound): kind
        // 1 without the player (it rides the incoming screen), kind 2 with it
        // (the fade hides it).
        FrameBuffer old_center{};   // 320x200
        {
            // The fade: enter_cave/exit_cave already moved the player and
            // flipped cave_flag/current_screen.  The old screen's assets are
            // still bound, so the state as the frame before showed it
            // reproduces the true outgoing frame; without it the player is
            // drawn at the new position over the old background.  The pose
            // too: descent frame 46 + dx on cave entry, the pre-emerge walk
            // sprite on cave exit.  Teleport tick fields are not swapped: on
            // the consume tick both values hide the player, and the clouds
            // are drawn by game_app hooks.  advance_state=false: render-only
            // (the per-tick advance is the main fb compose), as ShownAs
            // requires.
            const bool fade = trans.kind == TransitionKind::kFadePair;
            std::optional<ShownAs> as_before;
            if (fade) as_before.emplace(g.state, ShownFields::before(pf));
            RenderTarget rt{old_center.px.data(), 320, 200, 1, nullptr,
                            nullptr};
            rt.advance_state = false;
            compose_frame(rt, g.state, g.render, /*draw_player=*/fade);
        }

        // Wrap with the old side's peek rule; whether the wide path runs is
        // decided after the rebind (ws_old || ws_new).  A kind-2 fade-out must
        // carry the same margins as the steady view it fades from: the peek
        // cache and seam lists are still the old screen's, so wrap_wide_static
        // reproduces them (wrap_wide's mirror/torus shows stale content).
        // Restore the outgoing screen number so per-screen rules (L3 s9/s17
        // void, L7 cave-hall base) fire.  Cave/secret sides (ws_old false) keep
        // wrap_wide_for.
        if (ws_old && trans.kind == TransitionKind::kFadePair) {
            ShownFields old_screen = ShownFields::of(g.state);
            old_screen.screen = pf.screen;
            const ShownAs as_old(g.state, old_screen);
            wsp.wrap_wide_static(old_center, trans.old_wide);
        } else {
            wsp.wrap_wide_for(old_center, ws_old, trans.old_wide);
        }
    } else if (wsp.active() &&
               is_secret_slide(trans.kind)) {
        // kind 3/4: the wide old buffer was built in the classification block
        // (trans.slide_old_wide) while the secret state was live.
        ws_old = trans.slide_old_wide_ok;
    }
    return {ws_old, ws_eligible_kind};
}

// Step 9's screen change, except the L3 trunk-descent branch (it stays in the
// driver with its DescentCtx wiring).
void step9_screen_change(Loaded& g, const PrevFrame& pf,
                         TransitionState& trans, FramePresenter& fp,
                         FrameBuffer& fb, bool enhanced,
                         bool warp_fade) {
    WidescreenPresenter& wsp = *fp.wsp;
    const TransitionChoice c =
        classify_transition(pf, g.state, enhanced, warp_fade);
    trans.kind = c.kind;
    if (c.kind == TransitionKind::kPan) trans.dir = c.dir;
    trans.slide_old_wide_ok = false;   // rebuilt by a slide's capture
    capture_outgoing(g, trans, fp, fb);
    const WideOutgoing wide = compose_outgoing_wide(g, pf, trans, wsp);
    // The L3 trunk-descent branch does its own clear/bind and never reaches
    // here.
    systems::clear_per_screen_state(g.state);
    bind_screen(g, g.state.current_screen);
    wsp.update_cache();   // recompute peek for the new screen
    g.state.screen_change = false;

    // The original runs no gameplay on the frame after a screen change.
    g.state.transition_skip = true;

    // The new screen's status is known now (cache updated).  Go wide when
    // EITHER side is ws_present, so the bars never pop mid-transition.
    if (wsp.active() && wide.eligible_kind) {
        const bool ws_new = wsp.present_path();
        trans.wide = wide.old_wide || ws_new;
    }

    // The L3 trunk descent keeps trans.wide false: it has its own flow.
}

// The classified transition, after the new screen's first compose: the wide
// pan / fade / secret slide, or the classic 320 players.
void play_classified(Loaded& g, const PrevFrame& pf, TransitionState& trans,
                     FramePresenter& fp, FrameBuffer& fb,
                     TransitionShellCtx& tctx,
                     const std::function<void(RenderTarget&)>& bubble_hook) {
    WidescreenPresenter& wsp = *fp.wsp;
    if (trans.wide && (trans.kind == TransitionKind::kPan || trans.kind == TransitionKind::kFadePair)) {
        // Incoming wide buffer from the new screen's 320 centre (cache
        // now updated), player included: kind 1's player rides the
        // incoming screen, kind 2's fade hides it.
        const bool ws_new = wsp.present_path();
        FrameBuffer new_center{};   // 320x200
        {
            RenderTarget rt{new_center.px.data(), 320, 200, 1, nullptr,
                            nullptr};
            compose_frame(rt, g.state, g.render, /*draw_player=*/true,
                          bubble_hook);
        }

        // kind 1: continuous panorama pan for a simple +-1 surface
        // step; anything else (cave/secret, non-adjacent, level wrap)
        // uses the two-buffer slide.
        const bool panorama_ok =
            trans.kind == TransitionKind::kPan && !g.state.cave_flag &&
            !g.state.secret_flag && g.state.current_screen < 100 &&
            pf.screen < 100 &&
            std::abs(g.state.current_screen - pf.screen) == 1 &&
            (trans.dir == 'R' || trans.dir == 'L');
        if (std::getenv("OLDUVAI_WS_DEBUG") != nullptr)
            std::fprintf(stderr,
                         "[WS-TRANS] kind=%d dir=%c %d->%d cave=%d "
                         "secret=%d => %s\n",
                         static_cast<int>(trans.kind), trans.dir, pf.screen,
                         g.state.current_screen, g.state.cave_flag,
                         g.state.secret_flag,
                         panorama_ok ? "panorama" : "legacy");

        if (panorama_ok) {
            play_panorama_wide(tctx, pf.screen,
                               g.state.current_screen, new_center);
        } else {
            std::vector<std::uint8_t> new_wide;

            // The new frame must carry the same margins as the steady
            // view it hands off to.  wrap_wide torus-wraps far-edge
            // columns (L3 screen 12 showed the right platforms in the
            // left margin for one frame); wrap_wide_static redraws the
            // bg-tile rows like the steady frame.  Bezel sides keep
            // black margins.
            if (ws_new) wsp.wrap_wide_static(new_center, new_wide);
            else        wsp.wrap_wide_for(new_center, ws_new, new_wide);
            play_transition_wide(tctx, trans.old_wide, new_wide,
                                 trans.kind, trans.dir);
        }
        trans.kind = TransitionKind::kNone;
    } else if (is_secret_slide(trans.kind)) {
        play_secret_slide(g, trans, fp, fb, tctx, bubble_hook);
    } else {
        play_transition(tctx, trans.old_frame, fb, trans.kind,
                        trans.dir);
    }
}

// The blocking players' context, built per transition (pace_last starts at
// 0).
TransitionShellCtx make_transition_ctx(Loaded& g, FramePresenter& fp,
                                       const TransitionState& trans,
                                       const TransitionEnv& env) {
    LevelSurface& surface = *fp.surface;
    TransitionShellCtx tctx;
    tctx.win = surface.win();
    tctx.running = env.running;
    tctx.draw_log = env.draw_log;
    tctx.frame_ms = env.frame_ms;
    tctx.smooth_motion = env.smooth_motion;
    tctx.hd = surface.hd();
    tctx.hd_scale = surface.hd_scale();
    tctx.hd_profile = surface.hd_profile();
    tctx.wsp = fp.wsp;
    tctx.hd_cache = &g.hd_cache;
    tctx.state = &g.state;
    tctx.render = &g.render;
    tctx.screen_count = static_cast<int>(g.tiles.screens.size());
    tctx.landing = trans.landing;
    tctx.upload_and_show = [&fp](FrameBuffer& f) { fp.present(f); };
    tctx.make_rt = [&g, &surface](FrameBuffer& b) {
        return make_render_target(b, surface, g.hd_cache);
    };
    tctx.compose_static = [&g](int s, FrameBuffer& out, bool frozen_full) {
        compose_surface_screen_static(g, s, out, nullptr, nullptr, frozen_full);
    };
    tctx.compose_wide_native = [&g](int s, int margin,
                                    const FrameBuffer* backdrop,
                                    std::vector<std::uint8_t>& wide) {
        compose_surface_screen_wide_native(g, s, margin, backdrop, wide);
    };
    tctx.build_assets = [&g](int s, LevelRenderAssets& ra,
                             systems::SystemsState& st) {
        build_surface_screen_assets(g, s, ra, st);
    };
    return tctx;
}

}  // namespace

void change_screen(Loaded& g, const PrevFrame& pf, TransitionState& trans,
                   FramePresenter& fp, FrameBuffer& fb, bool enhanced,
                   DescentCtx descent) {
    // Classify and capture the old frame before the rebind; playback follows
    // the new screen's first compose.  L7 fake cave (12<->13): the EXE warps
    // instantly (25b2:07df skips the wipe); classic pans, enhanced fades (as
    // the reference).
    const bool warp_fade = take_warp_fade(g.state);
    if (!is_l3_trunk_descent(pf, g.state)) {
        step9_screen_change(g, pf, trans, fp, fb, enhanced, warp_fade);
        return;
    }
    // The 17->18 trunk-descent cinematic (l3_end_level.cpp): it clears
    // `running` on a window close and arms the smoke tail; no transition
    // follows.
    descent.prev_screen = pf.screen;
    descent.logical_w = fp.surface->lsz().w();
    descent.logical_h = fp.surface->lsz().h();
    run_l3_trunk_descent_sequence(descent);
    trans.kind = TransitionKind::kNone;
}

void play_screen_transition(Loaded& g, const PrevFrame& pf,
                            TransitionState& trans, FramePresenter& fp,
                            FrameBuffer& fb,
                            const std::function<void(RenderTarget&)>& bubbles,
                            const TransitionEnv& env) {
    TransitionShellCtx tctx = make_transition_ctx(g, fp, trans, env);
    play_classified(g, pf, trans, fp, fb, tctx, bubbles);
    trans.kind = TransitionKind::kNone;
}

}  // namespace olduvai::presentation
