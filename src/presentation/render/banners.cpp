// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/banners.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "presentation/render/banner_fx.hpp"
#include "presentation/render/text_overlay.hpp"

namespace olduvai::presentation {

// Animated enhanced banners (wall-clock driven, so motion is smooth at any
// refresh rate and independent of the 18 Hz logic tick):
// GET READY!       → fully visible for HOLD ms, then rockets straight up
//                    off the top over FLY ms (quadratic accel), then gone.
//                    Armed on the level-start rising edge of
//                    get_ready_counter (arm_tick).
// NOT ENOUGH FOOD! → colour gradient + a gentle vertical bob in place; a
//                    persistent status banner, so it rides the wall clock.
// DEFAULT effect is opts.banner_fx ("caveman": primal fire-and-blood);
// rainbow/fire/gold/pulse stay available.  `t/1000` is wall-clock seconds →
// all animation is refresh-rate-independent.  Under headless capture
// (--play-frames / --play-shot) the clock is the logic tick instead, so a
// captured frame is reproducible run to run (see the constructor).
BannerPresenter::BannerPresenter(enhance::HdText& hd_text,
                                 const systems::SystemsState& state,
                                 const std::string& banner_fx,
                                 bool deterministic_clock)
    : deterministic_clock_(deterministic_clock),
      hd_text_(hd_text), state_(state) {
    const char* bfx_env = std::getenv("OLDUVAI_BANNER_FX");
    gr_effect_ = bfx_env ? bfx_env : banner_fx;
    food_effect_ = bfx_env ? bfx_env : banner_fx;
}

Uint32 BannerPresenter::now_ms() const {
    if (deterministic_clock_) {
        // Logic tick clock: frame_counter x (1000/18) ms -- the same 55 ms the
        // loop paces at (game_app.cpp frame_ms).  Quantised to the logic tick,
        // so every present within one tick (incl. the capture re-present)
        // draws the identical banner.  See the constructor comment.
        return static_cast<Uint32>(state_.frame_counter) * (1000u / 18u);
    }
    return SDL_GetTicks();
}

void BannerPresenter::arm_tick() {
    if (state_.get_ready_counter == 0x11 && gr_prev_counter_ != 0x11) {
        gr_anim_start_ = now_ms();
        gr_anim_active_ = true;
    }
    gr_prev_counter_ = state_.get_ready_counter;
}

std::uint64_t BannerPresenter::key() const {
    if (suppressed_) return 0x9E3779B97F4A7C15ull;   // draws nothing
    if (gr_anim_active_) return 0;   // animating: changes every frame
    const int gate_screen = (state_.current_level == 3) ? 17 : 18;
    if ((state_.current_level == 1 || state_.current_level == 3 ||
         state_.current_level == 5 || state_.current_level == 7) &&
        state_.current_screen == gate_screen && state_.food_count < 45) {
        return 0;                    // bobbing: changes every frame
    }
    return 0x9E3779B97F4A7C15ull;     // draws nothing
}

void BannerPresenter::draw(std::vector<std::uint8_t>& b, int ow, int oh) {
    if (suppressed_) return;
    // draw_centered_overlay_row centres at the output midpoint, which maps
    // to native x≈160 in BOTH the plain and widescreen canvases (the center
    // 320 sits at wsp.margin()), so one call serves every present path.
    // The font cap is sized to the pre-baked box's glyph height (native px
    // → output px) so the text "fits the box, centered"; cap is
    // saved/restored so a HUD/menu draw sharing this overlay pass keeps its
    // size.  Gates mirror the pre-baked draws: GET-READY counter window
    // [2,17] (FUN_27f7_1277); food gate screen + food < 45 (FUN_263c_09ab).
    const int saved_cap = hd_text_.cap_px();
    const Uint32 now = now_ms();
    auto emit = [&](int cap_native, int baseline, const char* text,
                    const enhance::BannerShader& shader) {
        hd_text_.set_cap_px(
            std::max(1, static_cast<int>(cap_native * oh / 200.0 + 0.5)));
        draw_centered_overlay_row_banner(b, ow, oh, hd_text_, baseline, text,
                                         shader);
    };

    // GET READY! — caveman (default), hold then rocket up off the top.
    if (gr_anim_active_) {
        const long HOLD = 2000, FLY = 450;   // ms
        const long t = static_cast<long>(now - gr_anim_start_);
        if (t < HOLD + FLY) {
            float yoff = 0.0f;
            if (t > HOLD) {
                float p = static_cast<float>(t - HOLD) / FLY;
                if (p > 1.0f) p = 1.0f;
                yoff = -(p * p) * 175.0f;   // quadratic accel, off the top
            }
            emit(12, 112 + static_cast<int>(yoff), "GET READY!",
                 enhance::BannerShader(gr_effect_, t / 1000.0f));
        } else {
            gr_anim_active_ = false;   // animation finished
        }
    }

    // NOT ENOUGH FOOD! — fire (default) + gentle vertical bob.
    const int gate_screen = (state_.current_level == 3) ? 17 : 18;
    if ((state_.current_level == 1 || state_.current_level == 3 ||
         state_.current_level == 5 || state_.current_level == 7) &&
        state_.current_screen == gate_screen && state_.food_count < 45) {
        const long ft = static_cast<long>(now);
        const float bob = 3.0f * std::sin(ft * 0.006f);
        emit(11, 111 + static_cast<int>(std::lround(bob)),
             "NOT ENOUGH FOOD!", enhance::BannerShader(food_effect_, ft / 1000.0f));
    }
    hd_text_.set_cap_px(saved_cap);
}

}  // namespace olduvai::presentation
