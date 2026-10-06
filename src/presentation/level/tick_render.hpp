// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// A platform level's render of one logic tick: the compose that advances
// draw state, the once-per-tick counters around the HUD draw, and the
// present, or smooth motion's interpolated sub-frames.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <SDL.h>

#include "presentation/level/level_view_deps.hpp"
#include "presentation/render/dirty_rects.hpp"   // BlitRecord
#include "presentation/render/smooth_present.hpp"   // SmoothPacer

namespace olduvai::presentation {

struct Loaded;
struct LevelFx;
struct LevelDiag;
struct GameOptions;
struct PrevFrame;
struct FramePresenter;
struct FrameBuffer;
struct RenderTarget;
class LevelSurface;
class WidescreenPresenter;
class BannerPresenter;

class TickRender {
public:
    using Bubbles = std::function<void(RenderTarget&)>;

    // The view's deps, and the view's own buffer and presenters.
    TickRender(LevelSurface& surface, const LevelViewDeps& deps,
               FrameBuffer& fb, WidescreenPresenter& wsp, FramePresenter& fp,
               SmoothPacer pacer);

    // The tick's one compose that advances draw state (club swing,
    // death/cave-warp clear).  Every later draw this tick is display only.
    void compose(const Bubbles& bubbles);

    // Once per tick, after compose: the banner arm, the dust tail, the
    // teleport countdown, the HUD, then GET READY.  The HUD sits between the
    // last two as in the EXE (FUN_27f7_1277, DS:0x97e0).
    void advance_once(BannerPresenter& banners);

    // The tick through the live present path.  True when the vsync fill
    // consumed the tick: the caller's pacing must then skip its sleep.
    bool present(const PrevFrame& pf, const Bubbles& bubbles,
                 bool fluid_bubbles, int frame);

    // --play-shot of the live present path's output.
    void capture_shot(const std::string& path, bool hd, SDL_Renderer* ren,
                      const Bubbles& bubbles);

private:
    void compose_sub(const Bubbles& bubbles);
    bool steady_ok(const Bubbles& bubbles) const;
    void present_frame(const Bubbles& bubbles);

    Loaded& g_;
    LevelSurface& surface_;
    FrameBuffer& fb_;
    LevelFx& fx_;
    WidescreenPresenter& wsp_;
    FramePresenter& fp_;
    LevelDiag& diag_;
    const GameOptions& opts_;
    int hd_scale_;
    SmoothPacer pacer_;
    std::vector<BlitRecord> blits_;   // fb's, from its last compose
    float player_fx_ = 0.0f;   // the sub-frame's interpolated player
    float player_fy_ = 0.0f;
};

}  // namespace olduvai::presentation
