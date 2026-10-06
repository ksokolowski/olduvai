// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss/boss_view.hpp"

#include <SDL.h>

#include "presentation/diag/frame_stats.hpp"      // FrameStats, wire_overlay_stats
#include "presentation/game_app.hpp"              // GameOptions
#include "presentation/image_out.hpp"             // present_output
#include "presentation/render/boss_render.hpp"    // render_l4_victory_*
#include "presentation/window_util.hpp"           // aspect_logical

namespace olduvai::presentation {

namespace {

// Enhanced HUD: the baked HUD strip comes out of the arena background, and
// the bar's gradient goes to BossHud::draw_into.
BossHudBar hud_bar(LevelSurface& surface, BossAssets& assets) {
    return surface.use_hd_text() ? capture_boss_hud_bar(assets.bg)
                                 : BossHudBar{};
}

SmoothPacer smooth_pacer(LevelSurface& surface, bool smooth,
                         std::uint32_t frame_ms) {
    SmoothPacer p;
    p.frame_ms = frame_ms;
    p.discrete_n = smooth_subframe_count(surface.win());
    p.vsync = smooth_try_enable_vsync(surface.ren(), smooth);
    return p;
}

}  // namespace

BossView::BossView(LevelSurface& surface, const BossViewDeps& deps)
    : surface_(surface),
      deps_(deps),
      hud_(&surface.hd_text(), hud_bar(surface, deps.assets),
           &deps.fight.player.lives, &deps.fight.health()),
      ws_(surface, surface.hd() && deps.opts.aspect == "widescreen",
          aspect_logical(surface.hd_scale(), deps.opts.aspect)),
      fb_{surface.hd() ? 320 * surface.hd_scale() : 320,
          surface.hd() ? 200 * surface.hd_scale() : 200},
      screen_(surface,
              [this](const FrameBuffer& f, bool do_present) {
                  // Always 320x200: upscale the whole frame.
                  surface_.upload(f.px, 320, LevelSurface::Res::kNative);
                  surface_.show(surface_.tex());
                  if (do_present) present_output(surface_.ren());
              },
              deps.frame_ms),
      present_(screen_.fn()),
      text_deps_(surface.text_screen(deps.frame_ms)),
      pacer_(smooth_pacer(surface, deps.smooth, deps.frame_ms)),
      arena_(surface, ws_, hud_, fb_, hd_cache_) {
    BossFight& fight = deps_.fight;
    BossAssets& assets = deps_.assets;
    // Classic draws the lives with the game's 1bpp font.
    hud_.set_classic_font(&assets.charset, &assets.palette);
    // The HUD text overlay was the largest present cost on the platform side
    // (42% at 1280x720): its sinks count too.
    wire_overlay_stats(deps_.stats, surface.overlay());
    surface.set_stats(&deps_.stats);
    arena_.stats = &deps_.stats;
    arena_.arena_bg = &assets.bg;
    arena_.smooth_use_float = &smooth_pos_.use_float;
    arena_.smooth_fx = &smooth_pos_.fx;
    arena_.smooth_fy = &smooth_pos_.fy;
    arena_.draw_fight_sprites = deps_.draw_fight_sprites;
    // Fight frames and the L4 ride-off go wide with the clean-bg +
    // sprite-overflow compose (the ride-off moves horizontally; mirroring a
    // baked frame would clip it).  L2 and L6 victories do not move
    // horizontally and keep the mirror.  Fade and tally stay pillarboxed.
    // tests/boss_l4_victory pins the ride-off shot.
    arena_.wide_victory = [&fight] {
        return fight.level == 4 && fight.l4.win_flag >= 1;
    };
    arena_.draw_victory_native = [&fight, &assets](RenderTarget& rt) {
        render_l4_victory_frame(rt, assets, fight.player, fight.l4);
    };
    arena_.draw_victory_sprites = [&fight, &assets](RenderTarget& rt) {
        render_l4_victory_sprites(rt, assets, fight.player, fight.l4);
    };
}

void BossView::use_wide_canvas() {
    if (ws_.active)
        surface_.lsz().set(ws_.w * surface_.hd_scale(),
                           200 * surface_.hd_scale());
}

DisplayInfo BossView::display_info() const {
    DisplayInfo di = read_display_info(surface_.ren(), surface_.win());
    di.aspect = deps_.opts.aspect;
    di.hd = surface_.hd();
    di.hd_scale = surface_.hd_scale();
    di.ws_active = ws_.active;
    di.ws_margin = ws_.M;
    di.ws_native_w = ws_.w;
    return di;
}

}  // namespace olduvai::presentation
