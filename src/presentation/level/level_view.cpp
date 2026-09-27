// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/level/level_view.hpp"

#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "presentation/diag/frame_stats.hpp"       // wire_overlay_stats
#include "presentation/diag/level_diag.hpp"
#include "presentation/game_app.hpp"                // GameOptions
#include "presentation/level/level_fx.hpp"
#include "presentation/level/level_setup.hpp"       // warm_level_sprites
#include "presentation/level/level_state.hpp"
#include "presentation/menu/cheat_picker.hpp"
#include "presentation/render/logical_size.hpp"
#include "presentation/render/smooth_present.hpp"

namespace olduvai::presentation {

namespace {

WidescreenShellCtx ws_ctx(LevelSurface& surface, GameOptions& opts,
                          Loaded& g) {
    WidescreenShellCtx c;
    c.surface = &surface;
    c.aspect = &opts.aspect;   // live: the pause edits it
    c.state = &g.state;
    c.render = &g.render;
    c.hd_cache = &g.hd_cache;
    c.internal_level_id = g.config.internal_id;
    c.surface_screen_count = static_cast<int>(g.tiles.screens.size());
    c.level_visual_background = g.config.visual_background;
    c.compose_static =
        [&g](int s, FrameBuffer& out, LevelRenderAssets* ra,
             const std::vector<LevelRenderAssets::TileDraw>* underlay,
             bool frozen_full, bool peek_monsters) {
            compose_surface_screen_static(g, s, out, ra, underlay,
                                          frozen_full, peek_monsters);
        };
    c.collect_monsters = [&g](int s) {
        return collect_spawn_post_monsters(g, s);
    };
    return c;
}

// Smooth motion: fixed 18 Hz logic, each tick filled with vsync-paced frames
// at a continuous alpha, or, when the driver refuses vsync
// (OLDUVAI_NO_VSYNC=1 forces it), with discrete sub-frames.  The pacer
// carries each fill's overshoot into the next tick's budget.
SmoothPacer smooth_pacer(LevelSurface& surface, const GameOptions& opts,
                         std::uint32_t frame_ms) {
    SmoothPacer p;
    p.frame_ms = frame_ms;
    p.discrete_n = smooth_subframe_count(surface.win());
    p.vsync =
        smooth_try_enable_vsync(surface.ren(), opts.enhance.smooth_motion);
    return p;
}

}  // namespace

LevelView::LevelView(LevelSurface& surface, const LevelViewDeps& deps)
    : surface_(surface),
      deps_(deps),
      wsp_(ws_ctx(surface, deps.opts, deps.g)),
      fb_{320 * surface.hd_scale(), 200 * surface.hd_scale()},
      banners_(surface.hd_text(), deps.g.state, deps.opts.banner_fx,
               deps.opts.frames > 0 || !deps.opts.screenshot.empty()),
      upload_and_show_([this](FrameBuffer& f, bool with_hud,
                              bool do_present) {
          fp_.present(f, with_hud, do_present);
      }),
      tick_(surface, deps, fb_, wsp_, fp_,
            smooth_pacer(surface, deps.opts, deps.frame_ms)),
      screen_(surface,
              [this](const FrameBuffer& f, bool do_present) {
                  FrameBuffer copy = f;   // upload may mutate
                  fp_.present(copy, /*with_hud=*/false, do_present);
              },
              deps.frame_ms),
      present_(screen_.fn()),
      text_deps_(surface.text_screen(deps.frame_ms)),
      trans_(fb_.w, fb_.h) {
    const GameOptions& opts = deps_.opts;
    Loaded& g = deps_.g;
    LevelFx& fx = deps_.fx;
    LevelDiag& diag = deps_.diag;
    const CheatPicker& cheats = deps_.cheats;
    const int s = surface.hd_scale();
    // The logical canvas: widescreen, the wide buffer's own size (no bars);
    // otherwise aspect_logical.  wsp.sync_output() keeps it from here on.
    const LogicalDims ld = wsp_.active()
                               ? LogicalDims{wsp_.native_w() * s, 200 * s}
                               : aspect_logical(s, opts.aspect);
    surface.lsz().set(ld.w, ld.h);

    // The widescreen present draws these over its wide foreground.
    wsp_.set_draw_overlay_tail([&fx, &g](RenderTarget& t) { fx.draw(t, g); });
    // Peek cache for the entry screen (bind_screen already ran), then the FOND
    // backdrop for no-neighbour margins.
    wsp_.update_cache();
    wsp_.build_backdrop();

    // Every banner draw passes here, so an open menu hides them.
    const auto draw_banners = [this](const enhance::Canvas& cv) {
        if (deps_.menus_open()) return;
        banners_.draw(cv);
    };
    wsp_.set_draw_banners(draw_banners);
    // A visible banner returns key 0, which forces an overlay redraw.
    wsp_.set_banners_key([this] { return banners_.key(); });

    fp_.surface = &surface;
    fp_.render = &g.render;
    fp_.charset = &g.charset;
    fp_.hd_cache = &g.hd_cache;
    fp_.wsp = &wsp_;
    fp_.state = &g.state;
    fp_.cheats = &cheats;
    fp_.menu_shot_path = &diag.menu.shot_path;
    fp_.draw_cheat_rows_native = [&cheats, &g](FrameBuffer& f) {
        cheats.draw_native(f, g.charset);
    };
    fp_.draw_cheat_rows = [&cheats, &surface](const enhance::Canvas& cv) {
        cheats.draw_hd(cv, surface.hd_text());
    };
    fp_.draw_enhanced_banners = draw_banners;
    // One counter for both presenters: a frame goes through exactly one.
    fp_.stats = &diag.stats;
    wsp_.stats = &diag.stats;
    wire_overlay_stats(diag.stats, surface.overlay());

    if (s > 1) warm_level_sprites(g, s, opts.hd_profile);
}

void LevelView::play_transition(const PrevFrame& pf,
                                const TickRender::Bubbles& bubbles,
                                const TransitionEnv& env) {
    if (trans_.kind == TransitionKind::kNone) return;
    banners_.set_suppressed(true);
    play_screen_transition(deps_.g, pf, trans_, fp_, fb_, bubbles, env);
    banners_.set_suppressed(false);
}

DescentCtx LevelView::descent(bool& running) {
    DescentCtx d;
    d.surface = &surface_;
    d.wsp = &wsp_;
    d.g = &deps_.g;
    d.opts = &deps_.opts;
    d.running = &running;
    d.l3_smoke_tail = &deps_.fx.l3_smoke_tail;
    d.frame_ms = deps_.frame_ms;
    d.l3_smoke_tail_ticks = kL3SmokeTailTicks;
    d.upload_and_show = upload_and_show_;
    return d;
}

ReportFormService::FreezeDeps LevelView::report_deps(bool god_active,
                                                     int display_level,
                                                     int internal) {
    Loaded& g = deps_.g;
    const auto& spr = g.render.entity_sprites;
    return {[&g, god_active](FrameBuffer& out) {
                g.state.god_mode = god_active;
                RenderTarget prt{out.px.data(), out.w, out.h, 1, nullptr,
                                 nullptr};
                prt.advance_state = false;
                compose_frame(prt, g.state, g.render, /*draw_player=*/true);
            },
            [this](FrameBuffer& f) {
                upload_and_show_(f, /*with_hud=*/false, /*do_present=*/true);
            },
            [this, &g, display_level, internal](const FrameBuffer& shot,
                                                const BugAnnotations& ann) {
                write_report(g, shot, ann, display_level, internal);
            },
            g.charset,
            spr.size() > 33 ? &spr[33] : nullptr,   // the menus' bone cursor
            &g.render.palette,
            deps_.frame_ms};
}

DisplayInfo LevelView::display_info() const {
    DisplayInfo di = read_display_info(surface_.ren(), surface_.win());
    di.aspect = wsp_.aspect();
    di.hd = wsp_.hd();
    di.hd_scale = wsp_.hd_scale();
    di.ws_active = wsp_.active();
    di.ws_margin = wsp_.margin();
    di.ws_native_w = wsp_.native_w();
    return di;
}

// The shown frame is re-rendered on the live present path (the widescreen
// compose, or fb through the HD upscale); classic 1x has none, as it equals
// the native shot.  No bubble hook: cosmetic.
void LevelView::write_report(const Loaded& g, const FrameBuffer& shot,
                             const BugAnnotations& ann, int display_level,
                             int internal) {
    const bool shown = surface_.hd() || wsp_.present_path();
    write_bug_report_as_shown(
        {g.state, shot, g.render.entity_sprites, display_level, internal, ann,
         shown, display_info(), BossInfo{}},
        surface_.ren(), [this, &shot] {
            if (wsp_.present_path()) {
                wsp_.present(TickRender::Bubbles{}, /*do_present=*/false);
            } else {
                FrameBuffer copy = shot;
                upload_and_show_(copy, /*with_hud=*/true,
                                 /*do_present=*/false);
            }
        });
}

}  // namespace olduvai::presentation
