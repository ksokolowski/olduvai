// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/incremental_upscale.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include "enhance/upscale.hpp"

namespace olduvai::enhance {

namespace {

struct Box {
    int x0, y0, x1, y1;   // [x0, x1) x [y0, y1); empty when x0 >= x1
};

// The bounding box of the pixels that differ between two w x h frames.
Box changed_box(const std::vector<std::uint8_t>& a,
                const std::vector<std::uint8_t>& b, int w, int h) {
    Box box{w, h, 0, 0};
    const std::size_t row = static_cast<std::size_t>(w) * 4;
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* ra = a.data() + y * row;
        const std::uint8_t* rb = b.data() + y * row;
        if (std::memcmp(ra, rb, row) == 0) continue;
        box.y0 = std::min(box.y0, y);
        box.y1 = y + 1;
        int x = 0;
        while (std::memcmp(ra + x * 4, rb + x * 4, 4) == 0) ++x;
        int xe = w;
        while (std::memcmp(ra + (xe - 1) * 4, rb + (xe - 1) * 4, 4) == 0) --xe;
        box.x0 = std::min(box.x0, x);
        box.x1 = std::max(box.x1, xe);
    }
    return box;
}

}  // namespace

const std::vector<std::uint8_t>& IncrementalUpscaler::upscale(
    const std::vector<std::uint8_t>& px, int w, int h, int scale,
    const std::string& profile) {
    const bool same_shape = w == w_ && h == h_ && scale == scale_ &&
                            profile == profile_ && prev_.size() == px.size();
    const Box box = same_shape ? changed_box(prev_, px, w, h) : Box{0, 0, w, h};
    const long long area = static_cast<long long>(box.x1 - box.x0) *
                           (box.y1 - box.y0);
    if (same_shape && box.x0 >= box.x1) {
        ++counts_.reused;
        return out_;
    }
    if (!same_shape || area * 2 > static_cast<long long>(w) * h) {
        out_ = upscale_rgba(px, w, h, scale, profile);
        ++counts_.whole;
    } else {
        // The outputs that can change: the box grown by the reach; the
        // crop they read: grown by it again.  Both clamped to the frame.
        const Box grown{std::max(0, box.x0 - kReach),
                        std::max(0, box.y0 - kReach),
                        std::min(w, box.x1 + kReach),
                        std::min(h, box.y1 + kReach)};
        const int cx0 = std::max(0, grown.x0 - kReach);
        const int cy0 = std::max(0, grown.y0 - kReach);
        const int cx1 = std::min(w, grown.x1 + kReach);
        const int cy1 = std::min(h, grown.y1 + kReach);
        const int cw = cx1 - cx0, ch = cy1 - cy0;
        std::vector<std::uint8_t> crop(static_cast<std::size_t>(cw) * ch * 4);
        for (int y = 0; y < ch; ++y)
            std::memcpy(&crop[static_cast<std::size_t>(y) * cw * 4],
                        &px[(static_cast<std::size_t>(cy0 + y) * w + cx0) * 4],
                        static_cast<std::size_t>(cw) * 4);
        const std::vector<std::uint8_t> up =
            upscale_rgba(crop, cw, ch, scale, profile);
        // Paste the grown box's part.
        const int ow = w * scale, uw = cw * scale;
        const int bw = (grown.x1 - grown.x0) * scale;
        for (int y = grown.y0 * scale; y < grown.y1 * scale; ++y)
            std::memcpy(
                &out_[(static_cast<std::size_t>(y) * ow + grown.x0 * scale) * 4],
                &up[(static_cast<std::size_t>(y - cy0 * scale) * uw +
                     (grown.x0 - cx0) * scale) * 4],
                static_cast<std::size_t>(bw) * 4);
        ++counts_.partial;
    }
    prev_ = px;
    w_ = w;
    h_ = h;
    scale_ = scale;
    profile_ = profile;
    return out_;
}

LazyUpscaler::LazyUpscaler(const std::vector<std::uint8_t>& px, int w, int h,
                           int scale, std::string profile)
    : px_(px), w_(w), h_(h), scale_(scale), profile_(std::move(profile)),
      done_(static_cast<std::size_t>(w), false),
      out_(static_cast<std::size_t>(w) * scale * h * scale * 4) {}

void LazyUpscaler::ensure(int x0, int x1) {
    x0 = std::max(0, x0);
    x1 = std::min(w_, x1);
    int a = x0;
    while (a < x1) {
        if (done_[static_cast<std::size_t>(a)]) {
            ++a;
            continue;
        }
        int b = a;
        while (b < x1 && !done_[static_cast<std::size_t>(b)]) ++b;
        // Band [a, b) with kReach columns of context, clamped to the image.
        const int cx0 = std::max(0, a - IncrementalUpscaler::kReach);
        const int cx1 = std::min(w_, b + IncrementalUpscaler::kReach);
        const int cw = cx1 - cx0;
        std::vector<std::uint8_t> crop(static_cast<std::size_t>(cw) * h_ * 4);
        for (int y = 0; y < h_; ++y)
            std::memcpy(&crop[static_cast<std::size_t>(y) * cw * 4],
                        &px_[(static_cast<std::size_t>(y) * w_ + cx0) * 4],
                        static_cast<std::size_t>(cw) * 4);
        const std::vector<std::uint8_t> up =
            upscale_rgba(crop, cw, h_, scale_, profile_);
        const int ow = w_ * scale_, uw = cw * scale_;
        for (int y = 0; y < h_ * scale_; ++y)
            std::memcpy(&out_[(static_cast<std::size_t>(y) * ow + a * scale_) * 4],
                        &up[(static_cast<std::size_t>(y) * uw +
                             (a - cx0) * scale_) * 4],
                        static_cast<std::size_t>(b - a) * scale_ * 4);
        std::fill(done_.begin() + a, done_.begin() + b, true);
        a = b;
    }
}

}  // namespace olduvai::enhance
