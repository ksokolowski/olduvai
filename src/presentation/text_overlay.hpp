// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Output-resolution vector-text overlay.
//
// THE PROBLEM IT SOLVES.  In HD mode the scene is composed into a buffer at
// 320·hd_scale (≤ 1280×800 at scale 4) and SDL nearest-scales that texture up
// to the physical window via SDL_RenderSetLogicalSize.  On a ≥2560-wide
// desktop the window is 2560×1600 and the 1280×800 texture — text included —
// is blown up ~2×, so any glyphs drawn IN the compose buffer end up blocky.
//
// THE FIX.  After the scene has been RenderCopy'd to the window, draw the
// vector text into a SEPARATE buffer sized at the renderer's TRUE OUTPUT
// resolution (SDL_GetRendererOutputSize), upload it to a streaming texture,
// and blit it 1:1 over the scene with logical scaling DISABLED — so 1 buffer
// unit = 1 physical pixel and the glyphs are rasterised crisply at the window
// resolution regardless of how far the scene texture was scaled.  The text cap
// height at output res is 8 · output_w/320 px (the 8 px native cap scaled to
// the physical window width).
//
// Classic mode (hd_scale == 1) never uses this — its bitmap HUD is drawn into
// the 320×200 buffer exactly as before.

#pragma once

#include <SDL.h>

#include <cstdint>
#include <vector>

#include <functional>
#include <string>

namespace olduvai::enhance {
class HdText;
}

namespace olduvai::presentation {

struct HdTextRow;   // presentation/screens.hpp

// Owns a reusable output-resolution RGBA buffer + a streaming SDL texture.
// One per window/renderer; survives across frames (re-allocates only when the
// output size changes).
class TextOverlay {
public:
    TextOverlay() = default;
    ~TextOverlay();
    TextOverlay(const TextOverlay&) = delete;
    TextOverlay& operator=(const TextOverlay&) = delete;

    // Begin a frame's text pass.  Queries the renderer output size into
    // ow/oh, (re)allocates the transparent (alpha 0) buffer on size change,
    // clears it, and sets `font` to the output-res cap height (8·ow/320).
    // Returns false if the output size could not be queried (overlay skipped).
    bool begin(SDL_Renderer* ren, enhance::HdText& font, int& ow, int& oh);

    // The buffer to draw glyphs into (ow*oh*4 RGBA, owner-managed).
    std::vector<std::uint8_t>& buffer() { return buf_; }
    int width() const { return w_; }
    int height() const { return h_; }

    // Output-res cap height for a given output width (8 px native cap scaled
    // to the physical window width).
    static int cap_px_for(int output_w) { return 8 * output_w / 320; }

    // Upload the buffer and blit it 1:1 over the scene: disables logical
    // scaling (1 unit = 1 physical px), RenderCopy at output res, then
    // restores the HD logical size (logical_w/h) for the next scene frame.
    // The caller calls SDL_RenderPresent afterwards.
    void flush(SDL_Renderer* ren, int logical_w, int logical_h);

private:
    void ensure(SDL_Renderer* ren, int ow, int oh);

    std::vector<std::uint8_t> buf_;
    SDL_Texture* tex_ = nullptr;
    int w_ = 0;
    int h_ = 0;
};

// Draw one vector-text string horizontally centred at output resolution, with
// the baseline at native_baseline_y scaled to output height.  `font` must be
// sized to the output cap height already (TextOverlay::begin).  Colour
// 235,235,235.  Used by the loading/tally overlays.
void draw_centered_overlay_row(std::vector<std::uint8_t>& out, int ow, int oh,
                               const enhance::HdText& font,
                               int native_baseline_y, const std::string& text);

// Same centred placement, but each pixel is coloured by `shade` (see
// HdText::draw_styled / ShadeFn) instead of a flat 235,235,235 — used for the
// enhanced banner effects (fire / rainbow / gold / pulse).
void draw_centered_overlay_row_styled(
    std::vector<std::uint8_t>& out, int ow, int oh, const enhance::HdText& font,
    int native_baseline_y, const std::string& text,
    const std::function<void(float, float, std::uint8_t&, std::uint8_t&,
                             std::uint8_t&)>& shade);

// Draw the score-tally `rows` at output resolution with a FIXED-ANCHOR layout
// (mirrors the reference's _tally_anchors / _record_tally_rows).  The colon column
// (labels right-aligned) and value column (values left-aligned) are derived ONCE
// from fixed reference strings — NOT from the current row text — so the columns
// are identical every frame and the labels never slide as the counting digits
// change width.  Per row, `align` selects placement: 0 → centred (title rows),
// 1 → label (right-aligned ending at the colon column), 2 → value (left-aligned
// starting at the value column).  Colour 235,235,235.  Used by the HD tally
// overlay in game_app.cpp / boss_app.cpp.
void draw_tally_rows_overlay(std::vector<std::uint8_t>& out, int ow, int oh,
                             const enhance::HdText& font,
                             const std::vector<HdTextRow>& rows);

}  // namespace olduvai::presentation
