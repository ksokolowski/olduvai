// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/text_overlay.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>

#include "enhance/hd_text.hpp"
#include "presentation/render/logical_size.hpp"
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
    OvTimer(const OvTimer&) = delete;
    OvTimer& operator=(const OvTimer&) = delete;
};
}  // namespace

// One pass over the drawn bytes: flag each row with content in `used` and, if
// `hv` is set, hash the bytes a 64-bit word at a time (instrumented / verify
// runs only; a collision costs one stale overlay frame at 2^-64).
namespace {
void scan_rows(const std::vector<std::uint8_t>& b, int w, int h_px,
               std::vector<std::uint8_t>& used, std::uint64_t* hv) {
    const std::size_t row_bytes = static_cast<std::size_t>(w) * 4;
    const std::size_t words = row_bytes / 8;
    const unsigned char* p = b.data();
    used.assign(static_cast<std::size_t>(h_px), 0);
    // Hash64's mix, fused into this pass with the row flags (hash64.hpp notes
    // the exception).
    std::uint64_t h = 1469598103934665603ull;
    for (int y = 0; y < h_px; ++y) {
        const unsigned char* r = p + static_cast<std::size_t>(y) * row_bytes;
        std::uint64_t racc = 0;
        if (hv != nullptr) {
            for (std::size_t i = 0; i < words; ++i) {
                std::uint64_t wv;
                std::memcpy(&wv, r + i * 8, 8);
                racc |= wv;
                h = (h ^ wv) * 1099511628211ull;
            }
            for (std::size_t i = words * 8; i < row_bytes; ++i) {
                racc |= r[i];
                h = (h ^ r[i]) * 1099511628211ull;
            }
        } else {
            for (std::size_t i = 0; i < words; ++i) {
                std::uint64_t wv;
                std::memcpy(&wv, r + i * 8, 8);
                racc |= wv;
            }
            for (std::size_t i = words * 8; i < row_bytes; ++i) racc |= r[i];
        }
        used[static_cast<std::size_t>(y)] = racc != 0 ? 1 : 0;
    }
    if (hv != nullptr) *hv = h;
}
}  // namespace

void TextOverlay::ensure(SDL_Renderer* ren, int ow, int oh) {
    if (tex_ != nullptr && w_ == ow && h_ == oh) {
        // Same size: clear only the rows that held content at the last flush;
        // the rest are still zero.
        OvTimer t(clear_ms, perf_ms, stats_on);
        const std::size_t row = static_cast<std::size_t>(w_) * 4;
        for (int y = 0; y < h_ && y < static_cast<int>(buf_rows_.size()); ++y) {
            if (buf_rows_[static_cast<std::size_t>(y)] == 0) continue;
            std::memset(buf_.data() + static_cast<std::size_t>(y) * row, 0, row);
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
    buf_rows_.assign(static_cast<std::size_t>(oh), 0);  // zeroed below
    tex_rows_.assign(static_cast<std::size_t>(oh), 0);
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
    // Skip the 3.7 MB upload when nothing changed (TrimUI: upload 7.07 ms of a
    // 32.2 ms present; the RenderCopy 0.11 ms).
    if (skipped_) {
        // Nothing drawn: the texture is already right, so no hash and no
        // upload.
        if (stats_on && uploads_skipped != nullptr) ++*uploads_skipped;
        OvTimer tb(blit_ms, perf_ms, stats_on);
        const LogicalScalingOff out(ren, logical_w, logical_h);
        SDL_RenderCopy(ren, tex_, nullptr, nullptr);
        return;
    }

    bool need_upload = true;
    {
        // Row flags for the upload below and the next clear; instrumented /
        // verify runs also hash to skip an identical upload.
        OvTimer t(hash_ms, perf_ms, stats_on);
        if (stats_on || verify_) {
            std::uint64_t h = 0;
            scan_rows(buf_, w_, h_, buf_rows_, &h);
            if (verify_ && key_was_same_ && tex_has_content_ &&
                h != last_hash_) {
                std::fprintf(stderr,
                    "overlay-verify: a key claimed UNCHANGED but the drawn "
                    "bytes differ -- that key is missing an input.\n");
            }
            need_upload = !(tex_has_content_ && h == last_hash_);
            last_hash_ = h;
        } else {
            scan_rows(buf_, w_, h_, buf_rows_, nullptr);
        }
    }

    if (!need_upload) {
        if (stats_on && uploads_skipped != nullptr) ++*uploads_skipped;
    } else {
        OvTimer t(upload_ms, perf_ms, stats_on);
        if (!tex_has_content_) {
            // A fresh texture holds garbage: the first upload is the panel.
            SDL_UpdateTexture(tex_, nullptr, buf_.data(), w_ * 4);
        } else {
            // Upload runs of rows that have content now or had it in the
            // texture (those must be cleared, or moved text leaves old rows).
            // Separate runs, not the span between them.
            const std::size_t row = static_cast<std::size_t>(w_) * 4;
            int y = 0;
            while (y < h_) {
                const auto dirty = [&](int yy) {
                    const auto i = static_cast<std::size_t>(yy);
                    return buf_rows_[i] != 0 || tex_rows_[i] != 0;
                };
                if (!dirty(y)) { ++y; continue; }
                int end = y;
                while (end + 1 < h_ && dirty(end + 1)) ++end;
                const SDL_Rect rows{0, y, w_, end - y + 1};
                SDL_UpdateTexture(tex_, &rows,
                                  buf_.data() + static_cast<std::size_t>(y) * row,
                                  w_ * 4);
                y = end + 1;
            }
        }
        tex_rows_ = buf_rows_;
        tex_has_content_ = true;
    }
    OvTimer t(blit_ms, perf_ms, stats_on);
    // The overlay is at output resolution; the scene's size comes back after.
    const LogicalScalingOff out(ren, logical_w, logical_h);
    SDL_RenderCopy(ren, tex_, nullptr, nullptr);
}

void draw_centered_overlay_row(const enhance::Canvas& cv,
                               const enhance::HdText& font,
                               int native_baseline_y, const std::string& text) {
    const int w = font.measure(text);
    const int x = cv.w / 2 - w / 2;
    const int baseline_y =
        static_cast<int>(native_baseline_y * (cv.h / 200.0) + 0.5);
    font.draw(cv, x, baseline_y, text, {235, 235, 235});
}

void draw_centered_overlay_row_banner(const enhance::Canvas& cv,
                                      const enhance::HdText& font,
                                      int native_baseline_y,
                                      const std::string& text,
                                      const enhance::BannerShader& shader) {
    const int w = font.measure(text);
    const int x = cv.w / 2 - w / 2;
    const int baseline_y =
        static_cast<int>(native_baseline_y * (cv.h / 200.0) + 0.5);
    font.draw_banner(cv, x, baseline_y, text, shader);
}

void draw_tally_rows_overlay(const enhance::Canvas& cv,
                             const enhance::HdText& font,
                             const std::vector<HdTextRow>& rows) {
    // Fixed-anchor tally columns (as the reference's _tally_anchors), from
    // fixed strings and the canvas width only, so labels do not slide as
    // digits change.
    //   label_w : widest of the three labels
    //   gap     : 16 native px (_TALLY_VALUE_GAP)
    const int label_w = std::max({font.measure("BONUS:"), font.measure("LIFE:"),
                                   font.measure("SCORE:")});
    const int gap = static_cast<int>(std::lround(16.0 * cv.w / 320.0));
    //   unit_w  : widest value being drawn, floored at the score row's
    //             width.  The score is always six digits ("%06ld"), so the
    //             floor keeps the columns still; bonus and lives are
    //             unpadded (reserving six digits put the block off centre).
    int unit_w = font.measure("888888");
    for (const auto& row : rows) {
        if (row.align == 2 || row.align == 3)   // 3 = a width reservation
            unit_w = std::max(unit_w, font.measure(row.text));
    }
    const int content_w = label_w + gap + unit_w;
    const int left = cv.w / 2 - content_w / 2;
    const int colon_x = left + label_w;   // labels right-aligned, ending here
    const int value_x = colon_x + gap;    // values left-aligned, starting here

    for (const auto& row : rows) {
        if (row.align == 3) continue;   // reservation only — not drawn
        const int baseline_y =
            static_cast<int>(row.native_baseline_y * (cv.h / 200.0) + 0.5);
        int x;
        switch (row.align) {
            case 1:   // label — right-aligned, ending at the colon column
                x = colon_x - font.measure(row.text);
                break;
            case 2:   // value — left-aligned, starting at the value column
                x = value_x;
                break;
            default:  // 0 — centred (title rows)
                x = cv.w / 2 - font.measure(row.text) / 2;
                break;
        }
        font.draw(cv, x, baseline_y, row.text, {235, 235, 235});
    }
}

}  // namespace olduvai::presentation
