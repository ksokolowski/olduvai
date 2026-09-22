// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// TextOverlay's partial clear + upload, and HdText's glyph cache.
//
// The overlay uploads only the rows that hold text now or held it in the
// texture before.  The case that matters is MOVED text — the bobbing NOT
// ENOUGH FOOD banner: if the rows it left were not re-uploaded, its old
// position would stay on screen.  Software renderer on the dummy video
// driver, so the composited result can be read back headlessly.

#include "doctest/doctest.h"

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "enhance/hd_text.hpp"
#include "presentation/render/text_overlay.hpp"

using olduvai::enhance::HdText;
using olduvai::presentation::TextOverlay;
using olduvai::presentation::draw_centered_overlay_row;

namespace {

struct SoftTarget {
    SDL_Surface* surf = nullptr;
    SDL_Renderer* ren = nullptr;
    SoftTarget() {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_Init(SDL_INIT_VIDEO);
        surf = SDL_CreateRGBSurfaceWithFormat(0, 320, 200, 32,
                                              SDL_PIXELFORMAT_RGBA32);
        if (surf != nullptr) ren = SDL_CreateSoftwareRenderer(surf);
    }
    ~SoftTarget() {
        if (ren != nullptr) SDL_DestroyRenderer(ren);
        if (surf != nullptr) SDL_FreeSurface(surf);
        SDL_Quit();
    }
    // Lit pixels in output rows [y0, y1).
    int lit(int y0, int y1) const {
        int n = 0;
        const auto* p = static_cast<const std::uint8_t*>(surf->pixels);
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < surf->w; ++x) {
                const std::size_t o =
                    static_cast<std::size_t>(y) * surf->pitch + x * 4;
                if (p[o] > 64) ++n;
            }
        return n;
    }
};

bool load_font(HdText& f) {
    SDL_setenv("OLDUVAI_FONT",
               OLDUVAI_TEST_SOURCE_DIR "/assets/fonts/FreckleFace-Regular.ttf",
               1);
    return f.load(".", 1);
}

// One overlay pass: clear the target, draw `text` at native baseline `y`
// (or nothing when empty), composite.
void pass(SoftTarget& t, TextOverlay& ov, HdText& f, int y,
          const std::string& text) {
    SDL_SetRenderDrawColor(t.ren, 0, 0, 0, 255);
    SDL_RenderClear(t.ren);
    int ow = 0, oh = 0;
    if (ov.begin(t.ren, f, ow, oh) && !text.empty())
        draw_centered_overlay_row(ov.buffer(), ow, oh, f, y, text);
    ov.flush(t.ren, 0, 0);
}

}  // namespace

TEST_CASE("moved overlay text leaves nothing behind (shipping and stats paths)") {
    for (const bool stats : {false, true}) {
        CAPTURE(stats);
        SoftTarget t;
        REQUIRE(t.ren != nullptr);
        HdText f;
        REQUIRE(load_font(f));
        TextOverlay ov;
        double hash_ms = 0, upload_ms = 0, clear_ms = 0, blit_ms = 0;
        unsigned long skipped = 0;
        if (stats) {
            ov.stats_on = true;
            ov.hash_ms = &hash_ms;
            ov.upload_ms = &upload_ms;
            ov.clear_ms = &clear_ms;
            ov.blit_ms = &blit_ms;
            ov.uploads_skipped = &skipped;
            ov.perf_ms = 1.0;
        }

        pass(t, ov, f, 40, "GET READY!");
        const int high_before = t.lit(20, 50);
        CHECK(high_before > 0);
        CHECK(t.lit(140, 170) == 0);

        // Moved down: the old rows must be clear on screen, the new ones lit.
        pass(t, ov, f, 160, "GET READY!");
        CHECK(t.lit(20, 50) == 0);
        CHECK(t.lit(140, 170) > 0);

        // Nothing drawn: everything the texture showed must go.
        pass(t, ov, f, 0, "");
        CHECK(t.lit(0, 200) == 0);

        // And back: same pixels as the first pass.
        pass(t, ov, f, 40, "GET READY!");
        CHECK(t.lit(20, 50) == high_before);
    }
}

TEST_CASE("cached glyphs draw the same bytes as the first rasterisation") {
    HdText f;
    SoftTarget t;   // SDL init for the env call; no renderer use here
    REQUIRE(load_font(f));
    f.set_cap_px(24);
    std::vector<std::uint8_t> a(320 * 200 * 4, 0), b(320 * 200 * 4, 0);
    f.draw(a, 320, 200, 10, 100, "NOT ENOUGH FOOD!", 235, 235, 235);  // cold
    f.draw(b, 320, 200, 10, 100, "NOT ENOUGH FOOD!", 235, 235, 235);  // warm
    CHECK(a == b);
    // A second size is a separate cache entry, not a stale reuse.
    std::vector<std::uint8_t> c(320 * 200 * 4, 0);
    f.set_cap_px(12);
    f.draw(c, 320, 200, 10, 100, "NOT ENOUGH FOOD!", 235, 235, 235);
    CHECK(c != a);
}
