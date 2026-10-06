// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Upscales a run of frames that differ in a small part (a cinematic's moving
// sprites over a still backdrop), re-running the scaler only on what changed
// since the last frame.  Every profile is local: an output pixel reads input
// pixels within kReach of its own.  So the outputs that can change are the
// changed box grown by kReach, and those read only the box grown by twice
// that: upscaling that crop on its own equals the whole frame's upscale over
// the grown box, and the rest is the last output.  A frame that changed
// mostly (a pan) is upscaled whole.
//
// SDL-free.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olduvai::enhance {

class IncrementalUpscaler {
public:
    // How far, in native pixels, any profile's output reads its input
    // (the chained x4 profiles included): 2 is the least every profile
    // passes test_incremental_upscale with (xbr needs it), 4 keeps margin.
    static constexpr int kReach = 4;

    // `px` (w x h RGBA) at `scale` with `profile`, as upscale_rgba returns
    // it.  Valid until the next call.
    const std::vector<std::uint8_t>& upscale(const std::vector<std::uint8_t>& px,
                                             int w, int h, int scale,
                                             const std::string& profile);

    // Frames upscaled whole / partly / not at all (unchanged), for reports.
    struct Counts {
        int whole = 0, partial = 0, reused = 0;
    };
    Counts counts() const { return counts_; }

private:
    std::vector<std::uint8_t> prev_, out_;
    int w_ = 0, h_ = 0, scale_ = 0;
    std::string profile_;
    Counts counts_;
};

// Upscales a still image that a window scrolls across (a pan's strip) one
// column band at a time, as the window reaches it, so the first frame costs
// the window's upscale and not the whole image's.  A band upscaled with
// kReach columns of context on each side equals the whole image's upscale
// over the band (the reasoning above).
class LazyUpscaler {
public:
    // `px` (w x h RGBA) must outlive the upscaler.
    LazyUpscaler(const std::vector<std::uint8_t>& px, int w, int h, int scale,
                 std::string profile);

    // Make native columns [x0, x1) of hd() final.
    void ensure(int x0, int x1);
    // (w * scale) x (h * scale) RGBA; final where ensure() covered.
    const std::vector<std::uint8_t>& hd() const { return out_; }

private:
    const std::vector<std::uint8_t>& px_;
    int w_, h_, scale_;
    std::string profile_;
    std::vector<bool> done_;   // per native column
    std::vector<std::uint8_t> out_;
};

}  // namespace olduvai::enhance
