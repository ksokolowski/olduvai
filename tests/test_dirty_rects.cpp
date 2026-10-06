// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Dirty-rect arithmetic (src/presentation/render/dirty_rects.hpp).
//
// A merged or banded set that drops a pixel of the input leaves that pixel
// stale on screen, so every case checks coverage against a per-pixel mask of
// the input; extra pixels only cost upload time.
#include "doctest/doctest.h"
#include "presentation/render/dirty_rects.hpp"

#include <cstdint>
#include <vector>

using olduvai::presentation::add_dirty;
using olduvai::presentation::copy_rects;
using olduvai::presentation::DirtyRect;
using olduvai::presentation::merge_dirty;
using olduvai::presentation::to_row_bands;

namespace {

std::vector<bool> mask(const std::vector<DirtyRect>& v, int w, int h) {
    std::vector<bool> m(static_cast<std::size_t>(w) * h, false);
    for (const DirtyRect& r : v)
        for (int y = r.y0; y < r.y1; ++y)
            for (int x = r.x0; x < r.x1; ++x)
                m[static_cast<std::size_t>(y) * w + x] = true;
    return m;
}

// Every pixel of `in` is in `out`.
bool covers(const std::vector<DirtyRect>& out,
            const std::vector<DirtyRect>& in, int w, int h) {
    const auto a = mask(out, w, h), b = mask(in, w, h);
    for (std::size_t i = 0; i < a.size(); ++i)
        if (b[i] && !a[i]) return false;
    return true;
}

}  // namespace

TEST_CASE("add_dirty clips to the target and drops what is left empty") {
    std::vector<DirtyRect> v;
    add_dirty(v, {-5, -5, 10, 10}, 100, 50);
    add_dirty(v, {90, 40, 120, 80}, 100, 50);
    add_dirty(v, {200, 0, 210, 10}, 100, 50);   // wholly outside
    add_dirty(v, {5, 5, 5, 20}, 100, 50);       // zero width
    REQUIRE(v.size() == 2);
    CHECK(v[0] == DirtyRect{0, 0, 10, 10});
    CHECK(v[1] == DirtyRect{90, 40, 100, 50});
}

TEST_CASE("merge_dirty: the bounding box against the summed areas + slack") {
    // Diagonal overlap: box 225 against 100 + 100, so 25 px of slack merges
    // them and 24 does not; the distant third never joins.
    std::vector<DirtyRect> v{{0, 0, 10, 10}, {5, 5, 15, 15}, {80, 80, 90, 90}};
    const std::vector<DirtyRect> in = v;
    std::vector<DirtyRect> tight = v;
    merge_dirty(tight, 24);
    CHECK(tight.size() == 3);
    merge_dirty(v, 25);
    REQUIRE(v.size() == 2);
    CHECK(v[0] == DirtyRect{0, 0, 15, 15});
    CHECK(covers(v, in, 100, 100));
    // A sprite's old and new rects one step apart become one.
    std::vector<DirtyRect> step{{10, 10, 42, 42}, {12, 10, 44, 42}};
    merge_dirty(step, 0);
    REQUIRE(step.size() == 1);
    CHECK(step[0] == DirtyRect{10, 10, 44, 42});
}

TEST_CASE("merge_dirty: slack buys merges across a gap, never loses a pixel") {
    std::uint32_t seed = 777u;
    const auto next = [&seed](int n) {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(n));
    };
    const int w = 200, h = 120;
    for (const long long slack : {0LL, 64LL, 4096LL, 1LL << 40}) {
        for (int trial = 0; trial < 50; ++trial) {
            std::vector<DirtyRect> v;
            const int n = 1 + next(20);
            for (int i = 0; i < n; ++i) {
                const int x = next(w), y = next(h);
                add_dirty(v, {x, y, x + 1 + next(40), y + 1 + next(40)}, w, h);
            }
            const std::vector<DirtyRect> in = v;
            merge_dirty(v, slack);
            CHECK(covers(v, in, w, h));
            CHECK(v.size() <= in.size());
            if (slack == (1LL << 40)) CHECK(v.size() == 1);
        }
    }
}

TEST_CASE("to_row_bands: full-width bands over exactly the touched rows") {
    const int w = 64, h = 40;
    const std::vector<DirtyRect> in{
        {3, 2, 10, 6}, {40, 5, 50, 9}, {0, 20, 4, 22}, {10, 30, 11, 31}};
    const auto bands = to_row_bands(in, w);
    REQUIRE(bands.size() == 3);
    CHECK(bands[0] == DirtyRect{0, 2, w, 9});   // 2-6 and 5-9 overlap
    CHECK(bands[1] == DirtyRect{0, 20, w, 22});
    CHECK(bands[2] == DirtyRect{0, 30, w, 31});
    CHECK(covers(bands, in, w, h));
}

TEST_CASE("copy_rects copies inside the rects and nothing outside") {
    const int w = 16, h = 8;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 4, 7);
    std::vector<std::uint8_t> dst(src.size(), 0);
    const std::vector<DirtyRect> rects{{2, 1, 5, 3}, {10, 6, 16, 8}};
    copy_rects(dst.data(), src.data(), w, rects);
    const auto m = mask(rects, w, h);
    for (std::size_t p = 0; p < m.size(); ++p)
        for (int c = 0; c < 4; ++c)
            CHECK(dst[p * 4 + static_cast<std::size_t>(c)] == (m[p] ? 7 : 0));
}

using olduvai::presentation::BlitRecord;
using olduvai::presentation::diff_blits;
using olduvai::presentation::disjoint_cover;

TEST_CASE("diff_blits: an unchanged blit is not a change") {
    const std::vector<BlitRecord> prev{{1, 0, 0, {0, 0, 50, 50}},
                                       {2, 60, 0, {60, 0, 70, 10}}};
    std::vector<BlitRecord> cur = prev;
    std::vector<DirtyRect> out;
    diff_blits(prev, cur, out);
    CHECK(out.empty());
    // The small one moves: its old and new rect, never the big one's.
    cur[1] = {2, 62, 0, {62, 0, 72, 10}};
    diff_blits(prev, cur, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0] == DirtyRect{60, 0, 70, 10});
    CHECK(out[1] == DirtyRect{62, 0, 72, 10});
    // Same place, other asset (an animation frame): a change.
    out.clear();
    cur = prev;
    cur[0].key = 9;
    diff_blits(prev, cur, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0] == DirtyRect{0, 0, 50, 50});
    // A blit that appears or goes: its own rect.
    out.clear();
    cur = prev;
    cur.push_back({3, 5, 5, {5, 5, 8, 8}});
    diff_blits(prev, cur, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == DirtyRect{5, 5, 8, 8});
    out.clear();
    diff_blits(cur, prev, out);
    REQUIRE(out.size() == 1);
}

TEST_CASE("disjoint_cover: exactly the union, every pixel once") {
    std::uint32_t seed = 4242u;
    const auto next = [&seed](int n) {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(n));
    };
    const int w = 120, h = 90;
    for (int trial = 0; trial < 200; ++trial) {
        std::vector<DirtyRect> v;
        const int n = next(12);
        for (int i = 0; i < n; ++i) {
            const int x = next(w), y = next(h);
            add_dirty(v, {x, y, x + 1 + next(50), y + 1 + next(50)}, w, h);
        }
        const auto out = disjoint_cover(v);
        std::vector<int> hits(static_cast<std::size_t>(w) * h, 0);
        for (const DirtyRect& r : out)
            for (int y = r.y0; y < r.y1; ++y)
                for (int x = r.x0; x < r.x1; ++x)
                    ++hits[static_cast<std::size_t>(y) * w + x];
        const auto want = mask(v, w, h);
        bool exact = true;
        for (std::size_t i = 0; i < hits.size(); ++i)
            exact = exact && hits[i] == (want[i] ? 1 : 0);
        CHECK(exact);
    }
    // Two overlapping squares: a few rects, not one per row.
    const auto two = disjoint_cover({{0, 0, 10, 10}, {5, 5, 15, 15}});
    CHECK(two.size() == 3);
}
