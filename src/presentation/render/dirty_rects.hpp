// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Dirty rectangles: the arithmetic of the dirty present (dirty_frame.hpp),
// which repaints and uploads only where a frame's blits changed.  Pure, so it
// is tested without a renderer.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace olduvai::presentation {

// Half-open pixel rect [x0, x1) x [y0, y1).
struct DirtyRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    bool empty() const { return x0 >= x1 || y0 >= y1; }
    long long area() const {
        return empty() ? 0
                       : static_cast<long long>(x1 - x0) * (y1 - y0);
    }
    friend bool operator==(const DirtyRect& a, const DirtyRect& b) {
        return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
    }
    friend bool operator!=(const DirtyRect& a, const DirtyRect& b) {
        return !(a == b);
    }
};

inline DirtyRect bounding(const DirtyRect& a, const DirtyRect& b) {
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1),
            std::max(a.y1, b.y1)};
}

// `r` clipped to a w x h target, appended unless empty.
inline void add_dirty(std::vector<DirtyRect>& v, DirtyRect r, int w, int h) {
    r.x0 = std::max(r.x0, 0);
    r.y0 = std::max(r.y0, 0);
    r.x1 = std::min(r.x1, w);
    r.y1 = std::min(r.y1, h);
    if (!r.empty()) v.push_back(r);
}

// Fewer, larger rects: each upload is a driver call with a fixed cost, and a
// sprite's old and new rects mostly overlap.  Two rects merge when their
// bounding box covers at most `slack` pixels more than the two areas summed
// (their overlap counts twice in that sum).  Repeats until no pair merges; n
// is a few dozen at most.
inline void merge_dirty(std::vector<DirtyRect>& v, long long slack) {
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < v.size() && !merged; ++i) {
            for (std::size_t j = i + 1; j < v.size(); ++j) {
                const DirtyRect b = bounding(v[i], v[j]);
                if (b.area() <= v[i].area() + v[j].area() + slack) {
                    v[i] = b;
                    v.erase(v.begin() + static_cast<std::ptrdiff_t>(j));
                    merged = true;
                    break;
                }
            }
        }
    }
}

// The same rows as `v`, as full-width bands of a w-wide target, merged where
// they touch or overlap.  A band uploads without the repack a narrower rect
// needs on GLES2 (no GL_UNPACK_ROW_LENGTH there), at the cost of more bytes.
inline std::vector<DirtyRect> to_row_bands(std::vector<DirtyRect> v, int w) {
    std::sort(v.begin(), v.end(), [](const DirtyRect& a, const DirtyRect& b) {
        return a.y0 < b.y0;
    });
    std::vector<DirtyRect> out;
    for (const DirtyRect& r : v) {
        if (!out.empty() && r.y0 <= out.back().y1)
            out.back().y1 = std::max(out.back().y1, r.y1);
        else
            out.push_back({0, r.y0, w, r.y1});
    }
    return out;
}

inline DirtyRect intersect(const DirtyRect& a, const DirtyRect& b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1),
            std::min(a.y1, b.y1)};
}

// One HD block blit: what was drawn (the asset's cache source key), from
// where (its origin) and what it covered (the clipped rect).  Records equal at
// the same index of two frames' lists wrote the same pixels at the same point
// in the order.
struct BlitRecord {
    std::uint64_t key = 0;
    int ox = 0, oy = 0;
    DirtyRect r;

    friend bool operator==(const BlitRecord& a, const BlitRecord& b) {
        return a.key == b.key && a.ox == b.ox && a.oy == b.oy && a.r == b.r;
    }
    friend bool operator!=(const BlitRecord& a, const BlitRecord& b) {
        return !(a == b);
    }
};

// What changed between two frames' blit lists, appended to `out`: a record
// that differs at an index gives its old and its new rect, a record on one
// side only gives its own.  A pixel outside every such rect is covered by the
// same blits in the same order in both frames, so over the same background it
// is the same pixel.  A blit inserted early shifts every later index: that
// frame redraws more than it must, never less.
inline void diff_blits(const std::vector<BlitRecord>& prev,
                       const std::vector<BlitRecord>& cur,
                       std::vector<DirtyRect>& out) {
    const std::size_t n = std::max(prev.size(), cur.size());
    for (std::size_t i = 0; i < n; ++i) {
        const bool p = i < prev.size(), c = i < cur.size();
        if (p && c && prev[i] == cur[i]) continue;
        if (p) out.push_back(prev[i].r);
        if (c) out.push_back(cur[i].r);
    }
}

// `v`'s union as disjoint rects: horizontal slabs between the rects' y edges,
// each slab's covering x intervals merged, a rect extended down while the
// next slab has the same interval.  A draw clipped to the result touches each
// pixel once, which a soft-edged blit needs (drawing it twice darkens the
// edge).
inline std::vector<DirtyRect> disjoint_cover(const std::vector<DirtyRect>& v) {
    std::vector<int> ys;
    for (const DirtyRect& r : v) {
        if (r.empty()) continue;
        ys.push_back(r.y0);
        ys.push_back(r.y1);
    }
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    std::vector<DirtyRect> out;
    std::vector<std::size_t> open, next_open;   // indices into `out`
    std::vector<std::pair<int, int>> iv;
    for (std::size_t k = 0; k + 1 < ys.size(); ++k) {
        const int y0 = ys[k], y1 = ys[k + 1];
        iv.clear();
        for (const DirtyRect& r : v)
            if (!r.empty() && r.y0 <= y0 && r.y1 >= y1)
                iv.emplace_back(r.x0, r.x1);
        std::sort(iv.begin(), iv.end());
        next_open.clear();
        std::size_t i = 0;
        while (i < iv.size()) {
            int x0 = iv[i].first, x1 = iv[i].second;
            for (++i; i < iv.size() && iv[i].first <= x1; ++i)
                x1 = std::max(x1, iv[i].second);
            std::size_t hit = out.size();
            for (const std::size_t o : open)
                if (out[o].x0 == x0 && out[o].x1 == x1 && out[o].y1 == y0) hit = o;
            if (hit == out.size())
                out.push_back({x0, y0, x1, y1});
            else
                out[hit].y1 = y1;
            next_open.push_back(hit);
        }
        open.swap(next_open);
    }
    return out;
}

// Copy `rects` of a w-wide RGBA buffer from `src` into `dst`.
inline void copy_rects(std::uint8_t* dst, const std::uint8_t* src, int w,
                       const std::vector<DirtyRect>& rects) {
    for (const DirtyRect& r : rects) {
        const std::size_t n = static_cast<std::size_t>(r.x1 - r.x0) * 4;
        for (int y = r.y0; y < r.y1; ++y) {
            const std::size_t o =
                (static_cast<std::size_t>(y) * w + r.x0) * 4;
            std::memcpy(dst + o, src + o, n);
        }
    }
}

}  // namespace olduvai::presentation
