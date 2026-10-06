// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The dirty present the steady presents share: BlitDiff, the region a frame
// composed from blits changed since the last present, and DirtyFrame (the
// platform widescreen and the boss arena), a frame kept from the last present
// and repainted only there.
//
// Pass one runs the draw in record-only mode and diffs the blit list against
// the last present's (diff_blits).  The changed rects, plus the caller's
// non-blit layers (the HUD bars), become disjoint rects that are restored from
// the background, and pass two draws clipped to them.  A sprite redrawn in
// place (the L2 T-Rex's body, the L6 giant's) is not a change.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "enhance/enhanced_hud.hpp"
#include "presentation/diag/frame_stats.hpp"
#include "presentation/render/dirty_rects.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/render/level_surface.hpp"

namespace olduvai::presentation {

// A frame of blits over a keyed background: what changed since the last
// present is the blit lists' difference plus the caller's extra layers, this
// present's and the last one's.  Only while the background key holds.
class BlitDiff {
public:
    bool ready(std::uint64_t bg_key) const { return ok_ && bg_key == bg_key_; }

    // After ready(): the changed region, disjoint.
    const std::vector<DirtyRect>& region(const std::vector<BlitRecord>& cur,
                                         const std::vector<DirtyRect>& extra) {
        changed_.clear();
        diff_blits(prev_, cur, changed_);
        changed_.insert(changed_.end(), extra.begin(), extra.end());
        changed_.insert(changed_.end(), prev_extra_.begin(), prev_extra_.end());
        region_ = disjoint_cover(changed_);
        return region_;
    }

    // This present becomes the last one.  `allow` false (a layer that records
    // no blits) makes the next present whole.  Takes `cur`'s contents.
    void commit(std::vector<BlitRecord>& cur,
                const std::vector<DirtyRect>& extra, std::uint64_t bg_key,
                bool allow) {
        prev_.swap(cur);
        prev_extra_ = extra;
        bg_key_ = bg_key;
        ok_ = allow;
    }

private:
    std::vector<BlitRecord> prev_;
    std::vector<DirtyRect> prev_extra_, changed_, region_;
    std::uint64_t bg_key_ = 0;
    bool ok_ = false;
};

class DirtyFrame {
public:
    using MakeTarget = std::function<RenderTarget(std::uint8_t*)>;
    using Draw = std::function<void(RenderTarget&)>;

    // A w x h RGBA background and the key that names it: same key, same
    // pixels.
    struct Background {
        const std::vector<std::uint8_t>& px;
        std::uint64_t key;
        int w, h;
    };

    // Compose one present into frame(): `draw`'s blits over `bg`.  `extra`:
    // the rects of what the caller draws on top afterwards (opaque, so drawing
    // it again over itself changes nothing).  `allow` false (a layer that
    // records no blits, e.g. secret-room bubbles) takes the whole frame, and
    // the present after it too.  Returns the region that changed, for
    // LevelSurface::upload_dirty, or null for the whole frame.
    const std::vector<DirtyRect>* compose(const Background& bg,
                                          const MakeTarget& make_target,
                                          const Draw& draw,
                                          const std::vector<DirtyRect>& extra,
                                          bool allow, FrameStats* stats) {
        const std::size_t n = static_cast<std::size_t>(bg.w) * bg.h * 4;
        const bool restore = LevelSurface::dirty_on() && allow &&
                             diff_.ready(bg.key) && frame_.size() == n &&
                             bg.px.size() == n;
        cur_.clear();
        const std::vector<DirtyRect>* region = nullptr;
        if (restore) {
            RenderTarget rec = make_target(frame_.data());
            rec.blits = &cur_;
            rec.record_only = true;
            draw(rec);
            region = &diff_.region(cur_, extra);
            {
                FrameStats::Timer bgc(stats, &FrameStats::bg_copy_ms);
                copy_rects(frame_.data(), bg.px.data(), bg.w, *region);
            }
            RenderTarget rt = make_target(frame_.data());
            rt.clip = region;
            draw(rt);
        } else {
            frame_.resize(n);
            {
                FrameStats::Timer bgc(stats, &FrameStats::bg_copy_ms);
                std::memcpy(frame_.data(), bg.px.data(),
                            std::min(n, bg.px.size()));
            }
            RenderTarget rt = make_target(frame_.data());
            rt.blits = &cur_;
            draw(rt);
        }
        diff_.commit(cur_, extra, bg.key, allow);
        return region;
    }

    std::vector<std::uint8_t>& frame() { return frame_; }

    // OLDUVAI_DIRTY_VERIFY: the same frame composed whole into a scratch
    // buffer, for LevelSurface::verify_dirty.  `finish` draws the caller's
    // layers on top, as it did on frame().
    const std::vector<std::uint8_t>& reference(
        const std::vector<std::uint8_t>& bg, const MakeTarget& make_target,
        const Draw& draw, const std::function<void(std::vector<std::uint8_t>&)>&
                              finish) {
        ref_.assign(bg.begin(), bg.end());
        ref_.resize(frame_.size());
        RenderTarget rt = make_target(ref_.data());
        draw(rt);
        finish(ref_);
        return ref_;
    }

private:
    std::vector<std::uint8_t> frame_, ref_;
    std::vector<BlitRecord> cur_;
    BlitDiff diff_;
};

// The HUD bars' boxes and fills as rects of a w x h frame at scale `s`, the
// bars drawn `margin` native px right of the frame's left edge.
inline void add_hud_rects(std::vector<DirtyRect>& v,
                          const enhance::EnhancedHudLayout& hud, int s,
                          int margin, int w, int h) {
    for (const auto& b : hud.boxes)
        add_dirty(v,
                  {(b.x + margin) * s, b.y * s, (b.x + b.w + margin) * s,
                   (b.y + b.h) * s},
                  w, h);
    for (const auto& f : hud.fills)
        add_dirty(v,
                  {(f.x + margin) * s, f.y * s, (f.x + f.w + margin) * s,
                   (f.y + f.h) * s},
                  w, h);
}

}  // namespace olduvai::presentation
