// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/text_overlay.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>

#include "enhance/hd_text.hpp"
#include "presentation/sequence/screens.hpp"   // HdTextRow (full definition)
#include "presentation/window_util.hpp"   // create_stream_tex

namespace olduvai::presentation {

TextOverlay::~TextOverlay() {
    if (tex_ != nullptr) SDL_DestroyTexture(tex_);
}

// Scoped accumulator, same shape as the presenters' PresentTimer.
namespace {
struct OvTimer {
    double* accum; double perf_ms; bool on; Uint64 t0;
    OvTimer(double* a, double pm, bool o)
        : accum(a), perf_ms(pm), on(o && a != nullptr),
          t0(on ? SDL_GetPerformanceCounter() : 0) {}
    ~OvTimer() {
        if (on) *accum += static_cast<double>(SDL_GetPerformanceCounter() - t0)
                          * perf_ms;
    }
};
}  // namespace

// 64-bit-word FNV-1a.  Byte-at-a-time would cost more than the upload it
// saves; a word at a time is ~8x fewer operations, and this only ever decides
// whether to skip -- a collision costs one stale overlay frame, at 2^-64.
namespace {
// Hash the buffer AND record which rows carry content, in one pass over the
// bytes we are already obliged to read.  `lo`/`hi` come back as an inclusive
// row range, or lo > hi when the buffer is entirely transparent.
std::uint64_t hash_buf(const std::vector<std::uint8_t>& b, int w, int h_px,
                       int& lo, int& hi) {
    std::uint64_t hv = 1469598103934665603ull;
    const std::size_t row_bytes = static_cast<std::size_t>(w) * 4;
    const unsigned char* p = b.data();
    lo = h_px; hi = -1;
    for (int y = 0; y < h_px; ++y) {
        const unsigned char* r = p + static_cast<std::size_t>(y) * row_bytes;
        std::uint64_t racc = 0;
        const std::size_t words = row_bytes / 8;
        for (std::size_t i = 0; i < words; ++i) {
            std::uint64_t wv;
            std::memcpy(&wv, r + i * 8, 8);
            racc |= wv;
            hv = (hv ^ wv) * 1099511628211ull;
        }
        for (std::size_t i = words * 8; i < row_bytes; ++i) {
            racc |= r[i];
            hv = (hv ^ r[i]) * 1099511628211ull;
        }
        if (racc != 0) { if (y < lo) lo = y; hi = y; }
    }
    return hv;
}
}  // namespace

void TextOverlay::ensure(SDL_Renderer* ren, int ow, int oh) {
    if (tex_ != nullptr && w_ == ow && h_ == oh) {
        // Same size — clear to fully transparent.  Only the rows that held
        // content at the last flush can be dirty, so only those need erasing;
        // everything else is already zero and has been since the last resize.
        OvTimer t(clear_ms, perf_ms, stats_on);
        if (dirty_hi_ >= dirty_lo_) {
            const std::size_t row = static_cast<std::size_t>(w_) * 4;
            std::fill(buf_.begin() + static_cast<std::ptrdiff_t>(dirty_lo_ * row),
                      buf_.begin() + static_cast<std::ptrdiff_t>((dirty_hi_ + 1) * row),
                      static_cast<std::uint8_t>(0));
        }
        return;
    }
    if (tex_ != nullptr) {
        SDL_DestroyTexture(tex_);
        tex_ = nullptr;
    }
    w_ = ow;
    h_ = oh;
    tex_has_content_ = false;   // fresh texture: the next flush MUST upload
    dirty_lo_ = 0; dirty_hi_ = -1;   // freshly zeroed by the assign below
    buf_.assign(static_cast<std::size_t>(ow) * oh * 4, 0);  // transparent
    tex_ = create_stream_tex(ren, ow, oh);
    if (tex_ != nullptr) {
        SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
    }
}

bool TextOverlay::begin(SDL_Renderer* ren, enhance::HdText& font, int& ow,
                        int& oh, std::uint64_t key) {
    if (ren == nullptr) return false;
    if (!verify_init_) {
        verify_ = std::getenv("OLDUVAI_OVERLAY_VERIFY") != nullptr;
        verify_init_ = true;
    }
    int rw = 0, rh = 0;
    if (SDL_GetRendererOutputSize(ren, &rw, &rh) != 0 || rw <= 0 || rh <= 0) {
        return false;
    }
    // Decide BEFORE ensure(), because a skip must not clear the buffer either.
    const bool key_matches = key != kAlwaysRedraw && key == last_key_ &&
                             tex_has_content_ && tex_ != nullptr &&
                             w_ == rw && h_ == rh;
    key_was_same_ = key_matches;
    // Verify mode redraws even when the key says it need not, so the hash can
    // catch a key that is missing an input.
    const bool can_skip = key_matches && !verify_;
    ow = rw;
    oh = rh;
    skipped_ = can_skip;
    if (can_skip) return false;      // caller draws nothing; flush re-composites

    ensure(ren, rw, rh);
    if (tex_ == nullptr) return false;
    last_key_ = key;
    font.set_cap_px(cap_px_for(rw));
    return true;
}

void TextOverlay::flush(SDL_Renderer* ren, int logical_w, int logical_h) {
    if (tex_ == nullptr || w_ <= 0 || h_ <= 0) return;
    // Skip the 3.7 MB upload when the drawn bytes are identical to what the
    // texture already holds.  Measured on a TrimUI: the upload is 7.07 ms of a
    // 32.2 ms present call while the RenderCopy below is 0.11 ms -- building
    // the overlay is expensive, compositing it is free.
    if (skipped_) {
        // Nothing was drawn: the texture already holds the right pixels, so
        // neither the hash nor the upload is needed.  That is the whole point
        // of the key -- on the shipping path the 3.7 MB read never happens.
        if (stats_on && uploads_skipped != nullptr) ++*uploads_skipped;
        OvTimer tb(blit_ms, perf_ms, stats_on);
        SDL_RenderSetLogicalSize(ren, 0, 0);
        SDL_RenderCopy(ren, tex_, nullptr, nullptr);
        SDL_RenderSetLogicalSize(ren, logical_w, logical_h);
        return;
    }

    bool need_upload = true;
    if (stats_on || verify_) {
        // Instrumented or verifying: hash the drawn bytes.  This also yields
        // the dirty row extent for the next clear.
        OvTimer t(hash_ms, perf_ms, stats_on);
        const std::uint64_t h = hash_buf(buf_, w_, h_, dirty_lo_, dirty_hi_);
        if (verify_ && key_was_same_ && tex_has_content_ && h != last_hash_) {
            std::fprintf(stderr,
                "overlay-verify: a key claimed UNCHANGED but the drawn bytes "
                "differ -- that key is missing an input.\n");
        }
        need_upload = !(tex_has_content_ && h == last_hash_);
        last_hash_ = h;
    } else {
        // Shipping path: no hash, so the dirty extent is unknown and the next
        // clear must assume the whole buffer.  That costs a full memset on the
        // ~5% of calls that redraw, against a 3.7 MB read on every one of them
        // -- which is the trade the key exists to make.
        dirty_lo_ = 0;
        dirty_hi_ = h_ - 1;
    }

    if (!need_upload) {
        if (stats_on && uploads_skipped != nullptr) ++*uploads_skipped;
    } else {
        OvTimer t(upload_ms, perf_ms, stats_on);
        SDL_UpdateTexture(tex_, nullptr, buf_.data(), w_ * 4);
        tex_has_content_ = true;
    }
    OvTimer t(blit_ms, perf_ms, stats_on);
    // Disable logical scaling so 1 buffer unit maps to 1 physical pixel; the
    // overlay is already at output resolution.
    SDL_RenderSetLogicalSize(ren, 0, 0);
    SDL_RenderCopy(ren, tex_, nullptr, nullptr);
    // Restore the HD logical size for the next scene frame.
    SDL_RenderSetLogicalSize(ren, logical_w, logical_h);
}

void draw_centered_overlay_row(std::vector<std::uint8_t>& out, int ow, int oh,
                               const enhance::HdText& font,
                               int native_baseline_y, const std::string& text) {
    const int w = font.measure(text);
    const int x = ow / 2 - w / 2;
    const int baseline_y =
        static_cast<int>(native_baseline_y * (oh / 200.0) + 0.5);
    font.draw(out, ow, oh, x, baseline_y, text, 235, 235, 235);
}

void draw_centered_overlay_row_styled(
    std::vector<std::uint8_t>& out, int ow, int oh, const enhance::HdText& font,
    int native_baseline_y, const std::string& text,
    const std::function<void(float, float, std::uint8_t&, std::uint8_t&,
                             std::uint8_t&)>& shade) {
    const int w = font.measure(text);
    const int x = ow / 2 - w / 2;
    const int baseline_y =
        static_cast<int>(native_baseline_y * (oh / 200.0) + 0.5);
    font.draw_styled(out, ow, oh, x, baseline_y, text, shade);
}

void draw_tally_rows_overlay(std::vector<std::uint8_t>& out, int ow, int oh,
                             const enhance::HdText& font,
                             const std::vector<HdTextRow>& rows) {
    // Fixed-anchor tally columns (mirrors the reference's _tally_anchors).  Derived
    // ONLY from fixed reference strings + ow — NOT from the current row text —
    // so the colon/value columns are identical on every frame regardless of the
    // bonus/lives/score digit widths.  That is what stops the labels (and the
    // value column) from sliding while the counters run.
    //   label_w : widest of the three right-aligned labels
    //   gap     : 16 native px → output px (reference _TALLY_VALUE_GAP)
    //   unit_w  : widest possible value unit — "888888 x 10" / "888888 x 1000"
    //             / "888888" (6-digit fields; never narrower at runtime)
    const int label_w = std::max({font.measure("BONUS:"), font.measure("LIFE:"),
                                   font.measure("SCORE:")});
    const int gap = static_cast<int>(std::lround(16.0 * ow / 320.0));
    //   unit_w  : the widest VALUE actually being drawn, floored at the score
    //             row's width.  This used to reserve "888888  x  1000" — six
    //             digits in every field — but the real formats are "%d  x  10"
    //             (bonus, unpadded), "%d  x  1000" (lives, unpadded) and
    //             "%06ld" (score, always exactly six).  Only the score ever has
    //             six digits, so the reservation was far wider than anything
    //             drawn and the whole block sat left of centre inside it: rows
    //             measured -40, -27 and -50 px off the window centre while the
    //             title rows were exact.
    //
    //             The floor is what keeps the columns from sliding: the score
    //             is fixed-width, so unit_w cannot shrink below it as the
    //             counters run, and the anchors only move if a value is
    //             genuinely wider than the score — which is a real content
    //             change, not counter noise.
    int unit_w = font.measure("888888");
    for (const auto& row : rows) {
        if (row.align == 2) unit_w = std::max(unit_w, font.measure(row.text));
    }
    const int content_w = label_w + gap + unit_w;
    const int left = ow / 2 - content_w / 2;
    const int colon_x = left + label_w;   // labels right-aligned, ending here
    const int value_x = colon_x + gap;    // values left-aligned, starting here

    for (const auto& row : rows) {
        const int baseline_y =
            static_cast<int>(row.native_baseline_y * (oh / 200.0) + 0.5);
        int x;
        switch (row.align) {
            case 1:   // label — right-aligned, ending at the colon column
                x = colon_x - font.measure(row.text);
                break;
            case 2:   // value — left-aligned, starting at the value column
                x = value_x;
                break;
            default:  // 0 — centred (title rows)
                x = ow / 2 - font.measure(row.text) / 2;
                break;
        }
        font.draw(out, ow, oh, x, baseline_y, row.text, 235, 235, 235);
    }
}

}  // namespace olduvai::presentation
