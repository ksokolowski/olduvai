// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Enhanced animated banner substitutes for the pre-baked sprite "banners"
// (GET READY 132/133; NOT-ENOUGH-FOOD gate cue 82/91), drawn into the
// output-resolution overlay so they appear crisp AND survive the widescreen
// re-compose (the center pre-baked draw is recomposed away there).
// Extracted verbatim from run_platform_level (CC3 phase 7): the presenter
// owns the GET READY fly-away latch and the wall-clock animation; the shell
// keeps the enhanced-only gating (it only calls draw() on the use_hd_text
// overlay paths) and the once-per-logic-tick arm_tick() placement.

#pragma once

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "enhance/hd_text.hpp"
#include "systems/player.hpp"  // SystemsState

namespace olduvai::presentation {

class BannerPresenter {
public:
    // `hd_text` and `state` must outlive the presenter (they are
    // run_platform_level locals; the presenter lives in the same scope).
    // `banner_fx` is the configured effect; OLDUVAI_BANNER_FX overrides
    // BOTH banners for experimentation.
    BannerPresenter(enhance::HdText& hd_text,
                    const systems::SystemsState& state,
                    const std::string& banner_fx);

    // Once per logic tick: arm the GET READY fly-away on the level-start
    // rising edge of get_ready_counter (load_level sets it to 0x11).
    // Wall-clock start = now, so the hold + rocket-up play smoothly at any
    // refresh rate.
    void arm_tick();

    // Draw into the output-resolution overlay.  Enhanced-only by
    // construction — callers invoke this on the use_hd_text overlay path;
    // classic keeps the pre-baked sprites untouched.
    void draw(std::vector<std::uint8_t>& b, int ow, int oh);

private:
    enhance::HdText& hd_text_;
    const systems::SystemsState& state_;
    std::string gr_effect_;
    std::string food_effect_;
    // GET READY fly-away latch (wall-clock based).  gr_prev_counter_ inits
    // to a non-0x11 value so the very first level's arm (counter 0→0x11)
    // is detected.
    Uint32 gr_anim_start_ = 0;
    bool gr_anim_active_ = false;
    int gr_prev_counter_ = 0;
};

}  // namespace olduvai::presentation
