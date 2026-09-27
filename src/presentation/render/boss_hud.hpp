// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The boss arena HUD: the energy bar's captured geometry and the erase that
// leaves the arena background clean under it.  The enhanced HUD is redrawn
// every frame, so the baked strip in RING.PC1 must come out of the background
// first (else the old labels show underneath and the widescreen mirror
// reflects a black box).
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "enhance/canvas.hpp"

namespace olduvai::enhance { class HdText; }
namespace olduvai::formats { struct Sprite; struct Rgb; }

namespace olduvai::presentation {

struct FrameBuffer;

// The energy bar as baked into the background, captured before the erase, to
// redraw the bar in its own colours.
struct BossHudBar {
    // Leftmost green column of the bar in rows 0-5.  272 is what the scan finds
    // on the shipped RING.PC1; used when the scan does not run or finds
    // nothing.
    int left = 272;
    // Columns actually captured: left..kBossHealthStart inclusive.
    int strip_w = 0;
    // 6 rows x strip_w columns, RGBA — the original baked gradient.
    std::vector<std::uint8_t> strip;

    // The capture produced a usable gradient.  Without it the HUD shows a plain
    // ENERGY label and no bar (the classic look).
    bool ok() const { return strip_w > 0 && !strip.empty(); }
};

// Capture the baked energy bar from `bg` (RGBA 320x200, mutated), then erase
// the HUD strip: make_clean_boss_bg lifts the bright label and border pixels,
// and the (not bright) green bar columns are inpainted from the donor row
// below.  A buffer that is not 320x200 RGBA is left untouched and reported
// not ok().
BossHudBar capture_boss_hud_bar(std::vector<std::uint8_t>& bg);

// The boss HUD painter: the vector LIVES label and the framed energy gauge,
// into any RGBA buffer.  Holds pointers to the two numbers it shows, not the
// fight state.  SDL-free; the driver owns the overlay begin/flush.
class BossHud {
public:
    // Addresses of live cells (&player.lives, the fight's l2/l4/l6 .health),
    // read at draw time.
    BossHud(enhance::HdText* text, BossHudBar bar, const int* lives,
            const int* health)
        : text_(text), bar_(std::move(bar)), lives_(lives), health_(health) {}

    // Paint labels + bar into `buf` (bw x bh, output resolution: the renderer
    // size for the live overlay, or ws_w*hd_scale x 200*hd_scale for the wide
    // screenshot).  One mapping for both paths: `cx_native` is the centre's
    // native x (0, or the margin M), `total_native_w` the buffer's native width
    // (320 or 320+2M); native x maps to (cx_native + x) * bw/total_native_w, y
    // to y*bh/200.  (0, 320) reproduces the 320-only overlay exactly.
    // `draw_lives` false: the L2 victory flash, where the EXE does not redraw
    // the lives digit (labels are still drawn).
    void draw_into(const enhance::Canvas& cv,
                   bool draw_lives, int cx_native, int total_native_w);

    // Classic (non-HD): the same lives value in the game's 1bpp font, straight
    // into the 320x200 arena buffer at (48,8).  Kept in this type so the vector
    // and bitmap HUDs cannot drift apart.
    void set_classic_font(const std::vector<formats::Sprite>* charset,
                          const std::vector<formats::Rgb>* palette) {
        charset_ = charset;
        palette_ = palette;
    }
    void draw_classic_lives(FrameBuffer& fb) const;

    const BossHudBar& bar() const { return bar_; }

private:
    enhance::HdText* text_;
    BossHudBar bar_;
    const int* lives_;
    const int* health_;
    const std::vector<formats::Sprite>* charset_ = nullptr;
    const std::vector<formats::Rgb>* palette_ = nullptr;
};

}  // namespace olduvai::presentation
