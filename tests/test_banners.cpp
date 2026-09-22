// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// BannerPresenter's transition suppression (owner report, 2026-09-17): the
// NOT ENOUGH FOOD vector banner rode the slide into the food-gate screen.
// While a transition plays the presenter draws nothing and says so in its
// overlay key; otherwise it draws on the gate screen as before.

#include "doctest/doctest.h"

#include <SDL.h>

#include <cstdint>
#include <vector>

#include "enhance/hd_text.hpp"
#include "presentation/render/banners.hpp"

using olduvai::enhance::HdText;
using olduvai::presentation::BannerPresenter;

namespace {

int lit(const std::vector<std::uint8_t>& b) {
    int n = 0;
    for (std::size_t i = 3; i < b.size(); i += 4) n += b[i] != 0;
    return n;
}

}  // namespace

TEST_CASE("the food-gate banner is hidden while a transition plays") {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    REQUIRE(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_setenv("OLDUVAI_FONT",
               OLDUVAI_TEST_SOURCE_DIR "/assets/fonts/FreckleFace-Regular.ttf",
               1);
    HdText font;
    REQUIRE(font.load(".", 1));

    olduvai::systems::SystemsState st;
    st.current_level = 1;
    st.current_screen = 18;   // the L1 food gate
    st.food_count = 0;
    BannerPresenter banners(font, st, "caveman");

    const int ow = 448, oh = 200;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(ow) * oh * 4, 0);
    banners.draw(buf, ow, oh);
    CHECK(lit(buf) > 0);
    const auto drawing_key = banners.key();

    std::fill(buf.begin(), buf.end(), 0);
    banners.set_suppressed(true);
    banners.draw(buf, ow, oh);
    CHECK(lit(buf) == 0);
    CHECK(banners.key() != drawing_key);   // "draws nothing", not "redraw"

    std::fill(buf.begin(), buf.end(), 0);
    banners.set_suppressed(false);
    banners.draw(buf, ow, oh);
    CHECK(lit(buf) > 0);

    // Enough food: nothing to say, suppressed or not.
    st.food_count = 45;
    std::fill(buf.begin(), buf.end(), 0);
    banners.draw(buf, ow, oh);
    CHECK(lit(buf) == 0);
    SDL_Quit();
}
