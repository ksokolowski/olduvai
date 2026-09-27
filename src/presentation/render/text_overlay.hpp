// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Output-resolution vector-text overlay.  HD scenes are composed at
// 320*hd_scale and SDL scales them to the window, so text drawn in the scene
// buffer turns blocky on large displays.  This draws the text into a separate
// buffer at the renderer's true output size and blits it 1:1 over the scene
// with logical scaling off.  Cap height 8*output_w/320.  Classic (hd_scale 1)
// does not use it.

#pragma once

#include <SDL.h>

#include <cstdint>
#include <vector>

#include <functional>
#include <string>

#include "enhance/canvas.hpp"

namespace olduvai::enhance {
class HdText;
class BannerShader;
}

namespace olduvai::presentation {

struct HdTextRow;   // presentation/screens.hpp

// Reusable output-resolution RGBA buffer + streaming texture, one per
// renderer; reallocated only on an output size change.
class TextOverlay {
public:
    TextOverlay() = default;
    ~TextOverlay();
    TextOverlay(const TextOverlay&) = delete;
    TextOverlay& operator=(const TextOverlay&) = delete;

    // Begin a text pass: query the output size into ow/oh, (re)allocate and
    // clear the buffer, size `font` to the output cap height.  Returns false
    // when the size query fails, or when `key` matches what the texture already
    // shows (the caller then skips drawing and flush() re-composites).
    // kAlwaysRedraw (0, the default) disables the skip; a site opts in only
    // when its key covers everything it draws.  OLDUVAI_OVERLAY_VERIFY=1
    // redraws every time and reports any key that claimed "unchanged" while the
    // pixels moved.
    static constexpr std::uint64_t kAlwaysRedraw = 0;
    bool begin(SDL_Renderer* ren, enhance::HdText& font, int& ow, int& oh,
               std::uint64_t key = kAlwaysRedraw);

    // The buffer to draw glyphs into (ow*oh*4 RGBA, caller-owned).
    std::vector<std::uint8_t>& buffer() { return buf_; }
    int width() const { return w_; }
    int height() const { return h_; }

    // OLDUVAI_FRAME_STATS sinks (inert when stats_on is false).  The overlay's
    // cost is at output resolution (a 3.7 MB fill + a 3.7 MB upload per present
    // at 1280x720), so render_scale does not affect it.
    double* clear_ms = nullptr;    // std::fill / assign of the whole buffer
    double* upload_ms = nullptr;   // SDL_UpdateTexture of the whole buffer
    double* blit_ms = nullptr;     // RenderCopy + the two logical-size calls
    double* hash_ms = nullptr;     // the skip check itself
    unsigned long* uploads_skipped = nullptr;
    double perf_ms = 0.0;
    bool stats_on = false;

    // Output cap height for an output width (8 px native scaled).
    static int cap_px_for(int output_w) { return 8 * output_w / 320; }

    // Upload the buffer and blit it 1:1 over the scene (logical scaling off),
    // then restore the logical size logical_w/h.  The caller presents.
    void flush(SDL_Renderer* ren, int logical_w, int logical_h);

    // One pass: begin(), `draw(canvas)` over the buffer, flush().  `key` as
    // begin(): unchanged skips the paint and flush() re-composites; a failed
    // begin without a key flushes nothing.
    template <class Draw>
    void pass(SDL_Renderer* ren, enhance::HdText& font, int logical_w,
              int logical_h, Draw&& draw, std::uint64_t key = kAlwaysRedraw) {
        int ow = 0, oh = 0;
        if (begin(ren, font, ow, oh, key))
            draw(enhance::Canvas{buf_, ow, oh});
        else if (key == kAlwaysRedraw)
            return;
        flush(ren, logical_w, logical_h);
    }

private:
    void ensure(SDL_Renderer* ren, int ow, int oh);
    // Hash of the last uploaded buffer, computed from the drawn bytes, so it
    // cannot go stale like a caller key (six call sites draw into this buffer).
    std::uint64_t last_hash_ = 0;
    std::uint64_t last_key_ = kAlwaysRedraw;
    bool tex_has_content_ = false;
    bool skipped_ = false;      // this pass: caller drew nothing
    bool key_was_same_ = false; // this pass: the key claimed "unchanged"
    bool verify_ = false;       // OLDUVAI_OVERLAY_VERIFY
    bool verify_init_ = false;
    // Per-row content flags from the drawn bytes.  buf_rows_: rows the next
    // clear must erase.  tex_rows_: rows the texture shows; a partial upload
    // must cover them, or moved text leaves its old rows behind.
    std::vector<std::uint8_t> buf_rows_;
    std::vector<std::uint8_t> tex_rows_;

    std::vector<std::uint8_t> buf_;
    SDL_Texture* tex_ = nullptr;
    int w_ = 0;
    int h_ = 0;
};

// Draw one string centred at output resolution, baseline at native_baseline_y
// scaled to output height.  `font` is already sized (TextOverlay::begin).
// Colour 235,235,235.
void draw_centered_overlay_row(const enhance::Canvas& cv,
                               const enhance::HdText& font,
                               int native_baseline_y, const std::string& text);

// Same placement, each pixel coloured by a banner effect
// (enhance/banner_shader.hpp): the GET READY / NOT ENOUGH FOOD banners.
void draw_centered_overlay_row_banner(const enhance::Canvas& cv,
                                      const enhance::HdText& font,
                                      int native_baseline_y,
                                      const std::string& text,
                                      const enhance::BannerShader& shader);

// Draw the tally `rows` at output resolution with fixed anchors (as the
// reference): the colon and value columns come from fixed strings, so labels
// do not slide as digits change.  align 0 centred, 1 label (right-aligned to
// the colon column), 2 value (left-aligned from the value column).  Colour
// 235,235,235.
void draw_tally_rows_overlay(const enhance::Canvas& cv,
                             const enhance::HdText& font,
                             const std::vector<HdTextRow>& rows);

}  // namespace olduvai::presentation
