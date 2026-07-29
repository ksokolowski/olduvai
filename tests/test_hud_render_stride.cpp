// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// hud_render must stride by fb.w, not by a literal 320.
//
// A FrameBuffer is not always 320x200.  pause_service builds one at
// wsp.native_w() (448 at a 64px margin) for the widescreen pause menu and
// draws the bitmap glyphs into it whenever the vector overlay is not in use.
// While fill_rect/draw_glyph strided by a hardcoded 320, every glyph row in
// that buffer landed short and everything past x=320 was clipped — the pause
// menu came out sheared diagonally across the screen.
//
// No pixel golden caught it: they all run --enhanced, where the vector text
// path replaces the bitmap one.  So this drives the REAL draw_text entry point
// against a wide buffer.  Pinning fb_off's arithmetic alone would not do —
// hud_render could go back to a literal 320 and such a test would still pass.

// main() lives in tests/test_window_util.cpp for this target.
#include "doctest/doctest.h"

#include <cstddef>
#include <string>
#include <vector>

#include "presentation/render/game_render.hpp"
#include "presentation/render/hud_render.hpp"

using olduvai::formats::Rgb;
using olduvai::formats::Sprite;
using olduvai::formats::SpriteFormat;
using olduvai::presentation::FrameBuffer;
using olduvai::presentation::fb_off;

namespace {

// A charset whose every glyph is a solid 8x8 block, so any drawn character
// paints a predictable rectangle.  Index 'A' is all that gets used.
std::vector<Sprite> block_charset() {
    Sprite s;
    s.width = 8;
    s.height = 8;
    s.format = SpriteFormat::Mono1bpp;
    s.raw_pixels = std::vector<std::uint8_t>(8, 0xFF);   // 8 rows, all bits set
    return std::vector<Sprite>(128, s);
}

std::vector<Rgb> pal16() {
    std::vector<Rgb> p(16, Rgb{0, 0, 0});
    p[15] = Rgb{200, 100, 50};
    return p;
}

// Highest row index carrying any non-zero byte.
int last_touched_row(const FrameBuffer& fb) {
    for (int y = fb.h - 1; y >= 0; --y) {
        for (int x = 0; x < fb.w; ++x) {
            if (fb.px[fb_off(fb, x, y)] != 0) return y;
        }
    }
    return -1;
}

}  // namespace

TEST_CASE("draw_text into a WIDE buffer stays on the rows it was given") {
    FrameBuffer wide(448, 200);
    const auto cs = block_charset();
    const auto pal = pal16();

    // Baseline y=60 with an 8px-tall glyph occupies rows 52..59.
    olduvai::presentation::draw_text(wide, cs, pal, /*x=*/8, /*y=*/60, "A");

    const int last = last_touched_row(wide);
    REQUIRE(last >= 0);                 // something was actually drawn
    CHECK(last < 60);                   // never below the baseline
    CHECK(last >= 52);                  // and it did reach the glyph's rows

    // The decisive check.  With a literal-320 stride into a 448-wide buffer,
    // the byte for (x, y) lands at (y*320+x)*4, which for y in 52..59 falls
    // inside rows 37..42 of the real buffer — well above the requested band.
    // Assert nothing was written above row 50.
    for (int y = 0; y < 50; ++y) {
        for (int x = 0; x < wide.w; ++x) {
            REQUIRE(wide.px[fb_off(wide, x, y)] == 0);
        }
    }
}

TEST_CASE("draw_text reaches past x=320 in a wide buffer") {
    FrameBuffer wide(448, 200);
    const auto cs = block_charset();
    const auto pal = pal16();

    // x=400 is beyond the old hardcoded 320 clip, but well inside a 448 buffer.
    olduvai::presentation::draw_text(wide, cs, pal, /*x=*/400, /*y=*/60, "A");

    bool any = false;
    for (int y = 52; y < 60; ++y) {
        for (int x = 400; x < 408; ++x) {
            if (wide.px[fb_off(wide, x, y)] != 0) any = true;
        }
    }
    // The old code clipped every dx >= 320, so this region stayed blank.
    CHECK(any);
}

TEST_CASE("draw_text into a 320-wide buffer is unchanged") {
    FrameBuffer narrow(320, 200);
    const auto cs = block_charset();
    const auto pal = pal16();

    olduvai::presentation::draw_text(narrow, cs, pal, /*x=*/8, /*y=*/60, "A");

    // fb.w == 320 here, so the new arithmetic is identical to the old literal.
    // This is what the nine pixel goldens rely on.
    for (int y = 52; y < 60; ++y) {
        CHECK(narrow.px[(static_cast<std::size_t>(y) * 320 + 8) * 4 + 0] == 200);
    }
    CHECK(narrow.px[(static_cast<std::size_t>(51) * 320 + 8) * 4] == 0);
}
