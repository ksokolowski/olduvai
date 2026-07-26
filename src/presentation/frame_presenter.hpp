// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The single per-frame upload/composite/present pipeline, lifted out of
// run_platform_level (game_app.cpp).  Uploads the frame (native or HD-upscaled),
// pillarboxes under widescreen, then runs ONE output-resolution vector-text pass
// for the HUD text, the --cheats picker, and the pause/confirm menus (they must
// share a pass or a second begin/flush would not composite).  Holds POINTERS to
// the run-loop locals (so it always sees live values, like the old [&] lambda)
// plus the three sibling render callbacks it invokes.  Populate once before the
// loop; the FsPresentTimer stays in the caller's thin wrapper.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace olduvai::enhance {
class HdText;
}
namespace olduvai::systems {
struct SystemsState;
}

namespace olduvai::presentation {

struct FrameBuffer;
class WidescreenPresenter;
class TextOverlay;
class PauseService;

struct FramePresenter {
    WidescreenPresenter* wsp = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* tex = nullptr;
    enhance::HdText* hd_text = nullptr;
    TextOverlay* text_overlay = nullptr;
    PauseService* pause = nullptr;
    systems::SystemsState* state = nullptr;
    const int* logical_w = nullptr;
    const int* logical_h = nullptr;
    bool* cheat_open = nullptr;
    std::string* menu_shot_path = nullptr;
    bool hd = false;
    bool use_hd_text = false;
    int hd_scale = 1;
    // POINTER, not a copy: the HD profile is a LIVE Options setting (menus.json
    // marks it "live", and settings_apply returns ApplyTier::Live for a
    // same-scale swap), so the pause overlay / loading / tally upscales must see
    // a mid-level change like the steady + wide paths already do.  Holding it by
    // value froze it at loop entry and left those composites upscaling with the
    // old profile for the rest of the level.  Mirrors WidescreenPresenter and
    // TransitionPlayers, which both hold `const std::string*`.
    const std::string* hd_profile = nullptr;
    // Sibling render callbacks (game_app lambdas): the classic-buffer cheat
    // picker, the HD vector cheat rows, and the non-widescreen banner text.
    std::function<void(FrameBuffer&)> draw_cheat_rows_native;
    std::function<void(std::vector<std::uint8_t>&, int, int)> draw_cheat_rows;
    std::function<void(std::vector<std::uint8_t>&, int, int)> draw_enhanced_banners;

    void present(FrameBuffer& f, bool with_hud = true, bool do_present = true);
};

}  // namespace olduvai::presentation
