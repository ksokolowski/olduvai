// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Enhanced animated substitutes for the pre-baked banners (GET READY 132/133,
// NOT ENOUGH FOOD 82/91), drawn in the output overlay so they are crisp and
// survive the widescreen re-compose.  Owns the GET READY fly-away latch and
// the animation; the driver keeps the enhanced gating and the once-per-tick
// arm_tick().

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
    // `hd_text` and `state` must outlive the presenter.  `banner_fx`: the
    // configured effect (OLDUVAI_BANNER_FX overrides both banners).
    // `deterministic_clock`: animate on the logic tick (frame_counter x 1000/18
    // ms) instead of SDL_GetTicks(), for headless captures (PARITY T8).
    BannerPresenter(enhance::HdText& hd_text,
                    const systems::SystemsState& state,
                    const std::string& banner_fx,
                    bool deterministic_clock = false);

    // Once per logic tick: arm the fly-away on the get_ready_counter rising
    // edge (load_level sets 0x11); the animation then runs on the wall clock.
    void arm_tick();

    // Draw into the output overlay (enhanced paths only).
    void draw(const enhance::Canvas& cv);

    // Overlay-skip key; 0 = never skip, returned whenever anything would be
    // drawn (both banners animate).  Skipping cannot miss draw()'s one side
    // effect (clearing gr_anim_active_), which happens only while this returns
    // 0.
    std::uint64_t key() const;

    // Hide both banners during a screen transition (no banner over moving
    // scenery); they appear once the new screen is up.
    void set_suppressed(bool s) { suppressed_ = s; }

private:
    // The banner's "now": wall-clock in play, the logic tick clock under
    // headless capture (see the constructor).  Uint32 to match SDL_GetTicks.
    Uint32 now_ms() const;

    bool suppressed_ = false;
    bool deterministic_clock_ = false;
    enhance::HdText& hd_text_;
    const systems::SystemsState& state_;
    std::string gr_effect_;
    std::string food_effect_;
    // Fly-away latch.  gr_prev_counter_ starts off 0x11 so the first level's
    // arm is detected.
    Uint32 gr_anim_start_ = 0;
    bool gr_anim_active_ = false;
    int gr_prev_counter_ = 0;
};

}  // namespace olduvai::presentation
