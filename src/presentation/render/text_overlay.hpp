// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Output-resolution vector-text overlay.
//
// THE PROBLEM IT SOLVES.  In HD mode the scene is composed into a buffer at
// 320·hd_scale (≤ 1280x800 at scale 4) and SDL nearest-scales that texture up
// to the physical window via SDL_RenderSetLogicalSize.  On a ≥2560-wide
// desktop the window is 2560x1600 and the 1280x800 texture — text included —
// is blown up ~2x, so any glyphs drawn IN the compose buffer end up blocky.
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
// the 320x200 buffer exactly as before.

#pragma once

#include <SDL.h>

#include <cstdint>
#include <vector>

#include <functional>
#include <string>

namespace olduvai::enhance {
class HdText;
class BannerShader;
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
    // `key` summarises everything the caller is about to draw.  Returns false
    // when the overlay already on the texture is still correct: the caller then
    // skips drawing entirely and flush() re-composites what is there.
    //
    // key == kAlwaysRedraw (0) disables the optimisation for that call site,
    // which is the DEFAULT and how every un-converted caller behaves.  A site
    // opts in only once its key provably covers everything it draws.
    //
    // OLDUVAI_OVERLAY_VERIFY=1 forces a redraw every time and checks the key
    // against a hash of the drawn bytes, reporting any key that claimed
    // "unchanged" while the pixels moved.  Correctness is checkable without
    // the shipping path paying for a hash.
    static constexpr std::uint64_t kAlwaysRedraw = 0;
    bool begin(SDL_Renderer* ren, enhance::HdText& font, int& ow, int& oh,
               std::uint64_t key = kAlwaysRedraw);

    // The buffer to draw glyphs into (ow*oh*4 RGBA, owner-managed).
    std::vector<std::uint8_t>& buffer() { return buf_; }
    int width() const { return w_; }
    int height() const { return h_; }

    // OLDUVAI_FRAME_STATS sinks.  Inert when stats_on is false.
    //
    // WHY THE OVERLAY GOT ITS OWN.  The device measurement on 2026-09-09 put
    // 74.6% of present_total (23.96 ms of 32.1 ms PER PRESENT CALL) outside
    // both swap and the presenters' texture uploads, and this class is what
    // sits in the gap: at 1280x720 it std::fills 3.7 MB and SDL_UpdateTextures
    // another 3.7 MB on EVERY present, to redraw a HUD whose digits change a
    // few times a second.  Its cost is at OUTPUT resolution, so render_scale
    // does not touch it -- which is why the scaler and threading work, real
    // wins on their own terms, barely moved the transitions.
    double* clear_ms = nullptr;    // std::fill / assign of the whole buffer
    double* upload_ms = nullptr;   // SDL_UpdateTexture of the whole buffer
    double* blit_ms = nullptr;     // RenderCopy + the two logical-size calls
    double* hash_ms = nullptr;     // the skip check itself
    unsigned long* uploads_skipped = nullptr;
    double perf_ms = 0.0;
    bool stats_on = false;

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
    // Hash of the buffer last uploaded, and whether the texture holds it.
    // SAFE BY CONSTRUCTION: this is computed from the bytes actually drawn, so
    // it cannot go stale the way a caller-declared "nothing changed" key can.
    // That matters here because SIX call sites across four files draw into this
    // one buffer -- the level HUD, the widescreen HUD, banners, the boss HUD,
    // the pause menu and the confirm dialog -- and a key that missed one would
    // leave a stale dialog on screen in exactly the paths tests cover worst.
    std::uint64_t last_hash_ = 0;
    std::uint64_t last_key_ = kAlwaysRedraw;
    bool tex_has_content_ = false;
    bool skipped_ = false;      // this pass: caller drew nothing
    bool key_was_same_ = false; // this pass: the key claimed "unchanged"
    bool verify_ = false;       // OLDUVAI_OVERLAY_VERIFY
    bool verify_init_ = false;
    // Per-row content flags (1 = the row holds drawn bytes), both derived
    // from the drawn bytes at flush, so they cannot disagree with them the
    // way a declared key can.  buf_rows_: rows the next clear must erase.
    // tex_rows_: rows the TEXTURE shows from the last upload — a partial
    // upload must cover them too, or text that moved (a bobbing banner)
    // leaves its old rows behind.
    std::vector<std::uint8_t> buf_rows_;
    std::vector<std::uint8_t> tex_rows_;

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

// Same centred placement, each pixel coloured by a banner effect
// (enhance/banner_shader.hpp) — the GET READY / NOT ENOUGH FOOD banners.
void draw_centered_overlay_row_banner(std::vector<std::uint8_t>& out, int ow,
                                      int oh, const enhance::HdText& font,
                                      int native_baseline_y,
                                      const std::string& text,
                                      const enhance::BannerShader& shader);

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
