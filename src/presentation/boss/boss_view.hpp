// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// A boss fight's presentation, for the display's lifetime: the widescreen
// arena, the HUD, the arena buffer and its HD cache, the non-gameplay screens
// and the smooth-motion pacer, wired once.  It reads the fight; the fight does
// not read it.  BossDisplay owns it with its surface (presentation/display.hpp).
#pragma once

#include <cstdint>
#include <functional>

#include "enhance/hd_asset_cache.hpp"
#include "presentation/boss/boss_fight.hpp"         // BossFight, BossAssets, SmoothPos
#include "presentation/diag/bug_capture.hpp"         // DisplayInfo
#include "presentation/display.hpp"
#include "presentation/render/boss_arena.hpp"
#include "presentation/render/boss_hud.hpp"
#include "presentation/render/game_render.hpp"      // FrameBuffer, RenderTarget
#include "presentation/render/level_surface.hpp"
#include "presentation/render/smooth_present.hpp"   // SmoothPacer
#include "presentation/sequence/screen_presenter.hpp"
#include "presentation/sequence/screens.hpp"        // PresentFn

namespace olduvai::presentation {

struct FrameStats;
struct GameOptions;

// What a boss view reads, all of it outliving the view.
struct BossViewDeps {
    const GameOptions& opts;
    BossAssets& assets;    // loaded; the enhanced HUD strip is cut from its bg
    BossFight& fight;      // the HUD's lives and health; the L4 ride-off
    FrameStats& stats;
    // This fight's sprites over the clean arena (the wide present).
    std::function<void(RenderTarget&)> draw_fight_sprites;
    const bool& smooth;    // smooth motion: the pacer asks for vsync
    std::uint32_t frame_ms;
};

class BossView {
public:
    BossView(LevelSurface& surface, const BossViewDeps& deps);
    ~BossView() = default;
    BossView(const BossView&) = delete;
    BossView& operator=(const BossView&) = delete;

    BossWidescreen& ws() { return ws_; }
    FrameBuffer& fb() { return fb_; }
    enhance::HdAssetCache& hd_cache() { return hd_cache_; }
    ScreenPresenter& screen() { return screen_; }
    const PresentFn& present() const { return present_; }
    const TextScreenDeps& text_deps() const { return text_deps_; }
    BossArenaPresenter& arena() { return arena_; }
    SmoothPacer& pacer() { return pacer_; }
    SmoothPos& smooth_pos() { return smooth_pos_; }
    // A target over an arena buffer: HD carries the cache for the per-asset
    // path; classic is a scale-1 wrapper.
    RenderTarget target(FrameBuffer& b) {
        return make_render_target(b, surface_, hd_cache_);
    }

    // After the loading card: the wide logical canvas, in which 320-wide
    // presents (victory, tally) pillarbox.
    void use_wide_canvas();

    // How the fight is shown now: output, HD, widescreen (the F5 report).
    DisplayInfo display_info() const;
    LevelSurface& surface() { return surface_; }

private:
    LevelSurface& surface_;
    BossViewDeps deps_;
    BossHud hud_;
    // Widescreen (HD + aspect "widescreen", margin M > 0): fight and victory
    // frames present at (320+2M)x200, the margins a pure reflection of the
    // HUD-clean arena edges.  M follows the output size (Alt+Enter, resize).
    BossWidescreen ws_;
    enhance::HdAssetCache hd_cache_;
    // HD: output-sized, composed through the per-asset cache; classic 320x200.
    FrameBuffer fb_;
    // Loading card, post-win fade, classic tally: single 320x200 images.
    ScreenPresenter screen_;
    PresentFn present_;
    TextScreenDeps text_deps_;
    SmoothPacer pacer_;
    SmoothPos smooth_pos_;   // the fight's render and the wide overflow read it
    BossArenaPresenter arena_;
};

// The fight's display: the surface starts pillarboxed at the aspect's
// logical size, so the loading card is not stretched.
using BossDisplay = Display<BossView, BossViewDeps>;

}  // namespace olduvai::presentation
