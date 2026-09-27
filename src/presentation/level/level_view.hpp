// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// A platform level's presentation, for the display's lifetime: the widescreen
// presenter, the gameplay buffer, banners, the frame presenter, the tick
// render, the non-gameplay screens and the transition buffers, wired once.
// It reads the level, the effects and the menus; none of them reads it.
// LevelDisplay owns it with its surface and rebuilds both in place.
#pragma once

#include <cstdint>
#include <functional>

#include "presentation/diag/report_form.hpp"          // ReportFormService
#include "presentation/display.hpp"
#include "presentation/level/level_view_deps.hpp"
#include "presentation/level/tick_render.hpp"
#include "presentation/pipeline.hpp"
#include "presentation/render/banners.hpp"
#include "presentation/render/frame_presenter.hpp"
#include "presentation/render/game_render.hpp"        // FrameBuffer
#include "presentation/render/level_surface.hpp"
#include "presentation/render/widescreen_presenter.hpp"
#include "presentation/sequence/l3_end_level.hpp"     // DescentCtx
#include "presentation/sequence/screen_change.hpp"    // TransitionState
#include "presentation/sequence/screen_presenter.hpp"
#include "presentation/sequence/screens.hpp"          // PresentFn

namespace olduvai::presentation {

struct Loaded;
struct LevelFx;
struct LevelDiag;
struct GameOptions;
class CheatPicker;

class LevelView {
public:
    // fp.present(frame, with_hud, do_present), for the services that take a
    // function (the F5 form, the descent, the report).
    using UploadFn = std::function<void(FrameBuffer&, bool, bool)>;

    LevelView(LevelSurface& surface, const LevelViewDeps& deps);
    ~LevelView() = default;
    LevelView(const LevelView&) = delete;
    LevelView& operator=(const LevelView&) = delete;

    WidescreenPresenter& wsp() { return wsp_; }
    FrameBuffer& fb() { return fb_; }
    BannerPresenter& banners() { return banners_; }
    FramePresenter& fp() { return fp_; }
    TickRender& tick() { return tick_; }
    ScreenPresenter& screen() { return screen_; }
    const PresentFn& present() const { return present_; }
    const TextScreenDeps& text_deps() const { return text_deps_; }
    TransitionState& trans() { return trans_; }
    const UploadFn& upload_and_show() const { return upload_and_show_; }

    // The transition step 9 classified, if any, after the new screen's first
    // compose; no banner over a moving screen.
    void play_transition(const PrevFrame& pf, const TickRender::Bubbles& bubbles,
                         const TransitionEnv& env);

    // The L3 trunk descent's context, for step 9 (change_screen fills the
    // per-frame fields).  `running`: a window close mid-descent clears it.
    DescentCtx descent(bool& running);

    // The F5 form's view of the level, while the form is open: the frozen
    // scene (no state advance), the form frame and the report writer.  TODO:
    // the frozen scene lacks the widescreen composite for margin bugs; it
    // needs the bubble hook.
    ReportFormService::FreezeDeps report_deps(bool god_active,
                                              int display_level, int internal);

    // How the frame is shown now: output, HD, widescreen (the F5 report).
    DisplayInfo display_info() const;

private:
    // The F5 report: the clean frame, the notes, and the frame as shown.
    void write_report(const Loaded& g, const FrameBuffer& shot,
                      const BugAnnotations& ann, int display_level,
                      int internal);

    LevelSurface& surface_;
    LevelViewDeps deps_;
    WidescreenPresenter wsp_;
    // HD: output-sized, so every compose uses the per-asset cache; classic
    // 320x200.  In widescreen still the HD centre: the fallback for pause,
    // transition and screenshot, and the tick's one draw-state advance.
    FrameBuffer fb_;
    BannerPresenter banners_;
    FramePresenter fp_;
    UploadFn upload_and_show_;
    TickRender tick_;
    ScreenPresenter screen_;
    PresentFn present_;
    TextScreenDeps text_deps_;
    // The old screen's last frame and the classified effect, played after
    // the new screen's first compose.
    TransitionState trans_;
};

// The level's display (presentation/display.hpp): the surface starts at a
// 0x0 logical size, which the view's widescreen presenter sets.
using LevelDisplay = Display<LevelView, LevelViewDeps>;

}  // namespace olduvai::presentation
