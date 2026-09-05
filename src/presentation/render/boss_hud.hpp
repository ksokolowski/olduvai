// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The boss arena HUD: the energy bar's captured geometry, and the surgical
// erase that makes the arena background scene-clean underneath it.
//
// BACKLOG §3.18.  The enhanced boss HUD is recomposed as vector text + a
// redrawn bar on every frame, so the baked HUD strip in RING.PC1 has to come
// OUT of the background first — otherwise blit_bg paints the original labels
// under the new ones, and the widescreen mirror reflects a black box into the
// margins.  That erase and the gradient capture that must happen BEFORE it are
// one operation with one output, which is why they are one function here
// rather than fifty lines of run_boss_level's prologue.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace olduvai::enhance { class HdText; }
namespace olduvai::formats { struct Sprite; struct Rgb; }

namespace olduvai::presentation {

struct FrameBuffer;

// The energy bar as it was BAKED into the arena background, captured before
// the erase and used to redraw the bar with its own colours.
struct BossHudBar {
    // Leftmost green column of the bar in rows 0-5.  The 272 default is what
    // the scan finds on the shipped RING.PC1; it stands in when the scan does
    // not run (no HD text) or finds nothing green.
    int left = 272;
    // Columns actually captured: left..kBossHealthStart inclusive.
    int strip_w = 0;
    // 6 rows x strip_w columns, RGBA — the original baked gradient.
    std::vector<std::uint8_t> strip;

    // Whether the capture ran and produced a usable gradient.  Every drawing
    // site gates on this: without it the HUD falls back to a plain ENERGY
    // label and no bar, which is the classic (non-HD) look.
    bool ok() const { return strip_w > 0 && !strip.empty(); }
};

// Capture the baked energy bar out of `bg` (RGBA 320x200, MUTATED), then erase
// the whole HUD strip from it: make_clean_boss_bg lifts the bright label and
// border pixels, and the green bar columns — which are NOT bright, so that
// pass leaves them — are inpainted from the donor row below the strip, exactly
// as make_clean_boss_bg does for everything else.
//
// A buffer that is not 320x200 RGBA is left completely untouched and reported
// as a bar that is not ok(), which is the behaviour the prologue had: the
// size check guarded the whole block, capture and erase together.
BossHudBar capture_boss_hud_bar(std::vector<std::uint8_t>& bg);

// The boss HUD painter: the vector LIVES label and the framed energy gauge,
// drawn into an arbitrary RGBA buffer.
//
// BACKLOG §3.18 asked which of run_boss_level's 21 tangled locals a presenter
// may hold, and singled out the fight-state group (assets, player,
// internal_level, l2/l4/l6) with the objection that "a presenter that holds
// these is not a presenter".  That objection is right, and the answer is that
// the HUD does not need any of them.  It needs the two NUMBERS it displays.
//
// So this holds `const int*` into the live lives and health cells rather than
// the states that contain them.  `internal_level` is not a member either: it
// only ever SELECTED which of l2/l4/l6 to read, a decision made once, so it
// collapses into which address the caller binds.  What is left — a font, a
// captured gradient, and two integers to display — is genuinely presentation.
//
// SDL-free on purpose: it paints into a caller-supplied buffer and knows
// nothing about renderers or textures.  The live path (open the output-res
// text overlay, paint, flush it over the scene) stays in the driver, because
// that is the part that needs SDL — and §3.18 measured the three-lambda
// overlay trio at 22 branchless lines, i.e. not worth moving on its own.
class BossHud {
public:
    // `lives` and `health` are ADDRESSES of live cells (&player.lives, and
    // whichever of l2/l4/l6 .health this fight uses) — read at draw time, so
    // the HUD always shows the current frame's values.
    BossHud(enhance::HdText* text, BossHudBar bar, const int* lives,
            const int* health)
        : text_(text), bar_(std::move(bar)), lives_(lives), health_(health) {}

    // Paint labels + energy bar into `buf`, an RGBA buffer of OUTPUT-resolution
    // size (bw x bh) — the renderer output size for the live overlay, or
    // (ws_w*hd_scale x 200*hd_scale) for the offscreen wide screenshot.
    //
    // ONE implementation for both the non-widescreen and the widescreen path.
    // `cx_native` is the native x of the centre column (0 for the 320-wide /
    // pillarbox path, the margin M for the wide path); `total_native_w` is the
    // full native width of the composed buffer (320, or 320+2M).  Native HUD x
    // maps into the wide domain as (cx_native + x), then to output by
    // sx = bw/total_native_w.  With the defaults (0, 320) sx = bw/320 and there
    // is no x shift, so the 320-only overlay is reproduced byte for byte.  The
    // vertical scale sy = bh/200 is the same in both domains — height is always
    // 200 native.
    //
    // `draw_lives` false is the L2 victory flash, where the EXE never redraws
    // the lives digit; the labels ARE still drawn, per the reference.
    void draw_into(std::vector<std::uint8_t>& buf, int bw, int bh,
                   bool draw_lives, int cx_native, int total_native_w);

    // The CLASSIC (non-HD) stack: the same lives value, drawn with the game's
    // 1bpp font straight into the 320x200 arena buffer at (48,8).
    //
    // It lives here rather than staying in the driver's present because the
    // two stacks drifting apart is this codebase's recurring defect — §3.14
    // found one screen with two hand-copied presenters, and §3.14a found the
    // tally with three copies of one policy.  A HUD with a vector path in one
    // type and a bitmap path in a driver lambda is that shape starting again.
    // The font is presentation data, not fight state, so holding it is fine.
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
