// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/banners.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "presentation/render/banner_fx.hpp"
#include "presentation/render/text_overlay.hpp"

namespace olduvai::presentation {

// Animated enhanced banners on the wall clock (smooth at any refresh):
// GET READY!       visible for HOLD ms, then rockets off the top over FLY ms
//                  (quadratic); armed on the get_ready_counter rising edge.
// NOT ENOUGH FOOD! colour gradient + gentle bob in place while the status
//                  lasts.
// Effect: opts.banner_fx (default "caveman"; rainbow/fire/gold/pulse).  Under
// headless capture the clock is the logic tick, so captures are reproducible.
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
        // Logic-tick clock: frame_counter x (1000/18) ms, so every present in a
        // tick draws the same banner.
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

void BannerPresenter::draw(const enhance::Canvas& cv) {
    if (suppressed_) return;
    // draw_centered_overlay_row centres at the output midpoint, native x~160 in
    // both plain and widescreen canvases.  The cap matches the pre-baked box's
    // glyph height and is restored afterwards.  Gates as the pre-baked draws:
    // GET READY window [2,17] (FUN_27f7_1277); food gate screen + food < 45
    // (FUN_263c_09ab).
    const int saved_cap = hd_text_.cap_px();
    const Uint32 now = now_ms();
    auto emit = [&](int cap_native, int baseline, const char* text,
                    const enhance::BannerShader& shader) {
        hd_text_.set_cap_px(
            std::max(1, static_cast<int>(cap_native * cv.h / 200.0 + 0.5)));
        draw_centered_overlay_row_banner(cv, hd_text_, baseline, text, shader);
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
