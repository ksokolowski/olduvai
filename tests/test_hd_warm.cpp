// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The pre-warm must fill the cache with EXACTLY what lazy blitting would have
// put there — same keys, same bytes — whether it ran on one core or eight.
//
// WHY THIS GATE.  The warm's failure mode is silent and expensive in both
// directions.  Write entries under keys the blit never asks for and nothing
// breaks visually: the hitch simply stays exactly where it was, with the load
// now slower too.  Write the WRONG BYTES under keys the blit does ask for and
// the art is corrupt in a room that is hard to reach.  That second one is not
// hypothetical — key_of() omitted `bleed`, which changes the produced pixels,
// and the one caller that passes bleed=false is the fluid bubbles, which live
// in the very room this warm exists to speed up.  A warm shipped over that bug
// would have corrupted precisely what it was fixing.
//
// ALWAYS-GREEN — synthesised sprites, no game files — so it runs everywhere.
#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "enhance/hd_asset_cache.hpp"
#include "enhance/parallel_rows.hpp"
#include "formats/mat.hpp"
#include "formats/pc1.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/render/hd_warm.hpp"

using olduvai::enhance::HdAssetCache;
using olduvai::formats::Rgb;
using olduvai::formats::Sprite;
using olduvai::presentation::blit_sprite;
using olduvai::presentation::RenderTarget;
using olduvai::presentation::warm_hd_sprite_cache;

namespace {

// 5-plane ILBM: plane blocks of groups*h bytes, plane 4 the transparency mask.
// The bytes are a deterministic pattern rather than noise so a failure is
// reproducible, and the mask is deliberately mixed so the alpha-bleed and
// alpha-restamp branches both run — a fully-opaque sprite would pass even if
// those were skipped entirely.
Sprite make_sprite(int w, int h, unsigned salt) {
    Sprite s;
    s.width = static_cast<std::uint16_t>(w);
    s.height = static_cast<std::uint16_t>(h);
    s.format = olduvai::formats::SpriteFormat::Ilbm5;
    const std::size_t groups = static_cast<std::size_t>((w + 7) / 8);
    const std::size_t plane = groups * static_cast<std::size_t>(h);
    s.raw_pixels.resize(plane * 5);
    for (std::size_t i = 0; i < plane * 4; ++i)
        s.raw_pixels[i] = static_cast<std::uint8_t>((i * 37 + salt * 11) & 0xFF);
    for (std::size_t i = 0; i < plane; ++i)   // mask: holes, but not all holes
        s.raw_pixels[plane * 4 + i] =
            static_cast<std::uint8_t>((i % 5 == 0) ? 0x0F : 0xFF);
    return s;
}

std::vector<Sprite> make_sheet() {
    // Deliberately mixed sizes: equal-area sprites would hide a load-balance
    // bug in the dynamic index split, and an asymmetric set is what exercises
    // the flip_h path producing a SECOND distinct key.
    return {make_sprite(16, 16, 1), make_sprite(24, 32, 2),
            make_sprite(32, 24, 3), make_sprite(8, 8, 4),
            make_sprite(40, 17, 5), make_sprite(13, 29, 6)};
}

std::vector<Rgb> make_pal() {
    std::vector<Rgb> pal(16);
    for (std::size_t i = 0; i < pal.size(); ++i)
        pal[i] = Rgb{static_cast<std::uint8_t>(i * 17),
                     static_cast<std::uint8_t>(255 - i * 13),
                     static_cast<std::uint8_t>(i * 7 + 40)};
    return pal;
}

// blit_sprite's HD-path conversion, duplicated on purpose: if hd_warm.cpp's
// copy drifts from the renderer's, this test must NOT drift with it.
std::vector<std::uint8_t> ref_rgba(const Sprite& s, const std::vector<Rgb>& pal,
                                   bool flip_h) {
    const int w = s.width, h = s.height;
    const auto px = s.decode_indexed();
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto& p = px[static_cast<std::size_t>(y) * w +
                               static_cast<std::size_t>(flip_h ? (w - 1 - x) : x)];
            if (!p.opaque) continue;
            const Rgb c = (p.color < pal.size()) ? pal[p.color] : Rgb{255, 0, 255};
            const std::size_t o = (static_cast<std::size_t>(y) * w + x) * 4;
            out[o] = c.r; out[o + 1] = c.g; out[o + 2] = c.b; out[o + 3] = 255;
        }
    return out;
}

// Restores the flag however the test exits, so one failing REQUIRE cannot
// leave every later case in the binary running serially.
struct ThreadingScope {
    const bool prev = olduvai::enhance::parallel_rows_enabled();
    explicit ThreadingScope(bool on) {
        olduvai::enhance::set_parallel_rows_enabled(on);
    }
    ~ThreadingScope() { olduvai::enhance::set_parallel_rows_enabled(prev); }
};

constexpr int kScale = 3;

}  // namespace

TEST_CASE("hd warm: threaded result is byte-identical to serial") {
    const auto sheet = make_sheet();
    const auto pal = make_pal();

    HdAssetCache serial, threaded;
    std::size_t n_serial = 0, n_threaded = 0;
    { ThreadingScope off(false);
      n_serial = warm_hd_sprite_cache(serial, sheet, pal, kScale, "smooth"); }
    { ThreadingScope on(true);
      n_threaded = warm_hd_sprite_cache(threaded, sheet, pal, kScale, "smooth"); }

    CHECK(n_serial == n_threaded);
    CHECK(serial.size() == threaded.size());
    CHECK(serial.size() > 0);

    for (const auto& s : sheet)
        for (const bool flip : {false, true}) {
            const auto rgba = ref_rgba(s, pal, flip);
            const auto& a = serial.get(rgba, s.width, s.height, kScale, "smooth");
            const auto& b = threaded.get(rgba, s.width, s.height, kScale, "smooth");
            REQUIRE(a.w == b.w);
            REQUIRE(a.h == b.h);
            REQUIRE(a.px == b.px);
        }
}

TEST_CASE("hd warm: the warmed keys are the ones a blit asks for") {
    // The failure this pins: a warm that hashes a slightly different RGBA than
    // blit_sprite builds is 100% wasted work AND leaves the hitch in place,
    // with nothing visibly wrong to notice it by.
    //
    // DRIVEN THROUGH THE REAL RENDERER, not through a re-implementation of it.
    // An earlier version of this case called cache.get(ref_rgba(...)) — which
    // pinned the warm against the test's own copy and left the RENDERER free
    // to drift away from both, green all the way.  blit_sprite is the thing
    // whose keys have to match, so blit_sprite is what runs here.
    const auto sheet = make_sheet();
    const auto pal = make_pal();
    HdAssetCache cache;
    const std::size_t warmed =
        warm_hd_sprite_cache(cache, sheet, pal, kScale, "smooth");
    REQUIRE(warmed > 0);

    const std::size_t after_warm = cache.size();
    // w/h are the SCALED dimensions on the HD path — blit_hd_block bounds
    // dx/dy against them after multiplying by scale (widescreen_presenter.cpp
    // builds its wide target the same way).
    const std::string profile = "smooth";
    const int tw = 320 * kScale, th = 200 * kScale;
    std::vector<std::uint8_t> fb(
        static_cast<std::size_t>(tw) * th * 4, 0);
    RenderTarget t{fb.data(), tw, th, kScale, &cache, &profile};
    REQUIRE(t.hd_path());
    for (const auto& s : sheet)
        for (const bool flip : {false, true})
            blit_sprite(t, s, pal, 10, 10, flip);
    CHECK(cache.size() == after_warm);   // every blit was a hit: nothing rebuilt
}

TEST_CASE("hd warm: sprite_to_rgba is what an independent reading produces") {
    // ref_rgba is a deliberately SEPARATE implementation of the HD path's
    // conversion, so it can catch a change to the shared one that the two
    // callers would otherwise absorb in step with each other.
    const auto pal = make_pal();
    for (const auto& s : make_sheet())
        for (const bool flip : {false, true})
            CHECK(olduvai::presentation::sprite_to_rgba(s, pal, flip) ==
                  ref_rgba(s, pal, flip));

    // A SHORT palette, so the out-of-range fallback actually runs.  With the
    // full 16 entries it never does — 4-bit indices are all in range — so the
    // magenta branch was covered by neither implementation being exercised,
    // which a deliberate perturbation of it demonstrated by passing.
    const std::vector<Rgb> short_pal(pal.begin(), pal.begin() + 4);
    for (const auto& s : make_sheet())
        for (const bool flip : {false, true})
            CHECK(olduvai::presentation::sprite_to_rgba(s, short_pal, flip) ==
                  ref_rgba(s, short_pal, flip));
}

TEST_CASE("hd warm: warming twice builds nothing the second time") {
    const auto sheet = make_sheet();
    const auto pal = make_pal();
    HdAssetCache cache;
    CHECK(warm_hd_sprite_cache(cache, sheet, pal, kScale, "smooth") > 0);
    CHECK(warm_hd_sprite_cache(cache, sheet, pal, kScale, "smooth") == 0);
}

TEST_CASE("hd warm: classic scale is not warmed at all") {
    const auto sheet = make_sheet();
    const auto pal = make_pal();
    HdAssetCache cache;
    CHECK(warm_hd_sprite_cache(cache, sheet, pal, 1, "smooth") == 0);
    CHECK(cache.size() == 0);
}

TEST_CASE("hd cache: bleed is part of the key, both ways") {
    // The bug the warm would have shipped over.  The warm uses the default
    // bleed=true; the fluid bubbles ask with bleed=false.  Same source bytes,
    // so without `bleed` in the key the second call answers with the first
    // call's pixels — in the one room the warm exists to speed up.
    const auto sheet = make_sheet();
    const auto pal = make_pal();
    const auto rgba = ref_rgba(sheet[1], pal, false);
    const int w = sheet[1].width, h = sheet[1].height;

    HdAssetCache cache;
    const auto bled = cache.get(rgba, w, h, kScale, "smooth", true).px;
    const auto raw = cache.get(rgba, w, h, kScale, "smooth", false).px;
    CHECK(cache.size() == 2);      // two entries, not one
    CHECK(bled != raw);            // and they genuinely differ

    // A warm (bleed=true) must not satisfy a later bleed=false request.
    HdAssetCache warmed;
    warm_hd_sprite_cache(warmed, sheet, pal, kScale, "smooth");
    const std::size_t before = warmed.size();
    const auto after_warm_raw = warmed.get(rgba, w, h, kScale, "smooth", false).px;
    CHECK(warmed.size() == before + 1);       // it had to build a new entry
    CHECK(after_warm_raw == raw);             // and built the RIGHT one
}
