// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/text_overlay.hpp"

#include <algorithm>
#include <cmath>

#include "enhance/hd_text.hpp"
#include "presentation/screens.hpp"   // HdTextRow (full definition)
#include "presentation/window_util.hpp"   // create_stream_tex

namespace olduvai::presentation {

TextOverlay::~TextOverlay() {
    if (tex_ != nullptr) SDL_DestroyTexture(tex_);
}

void TextOverlay::ensure(SDL_Renderer* ren, int ow, int oh) {
    if (tex_ != nullptr && w_ == ow && h_ == oh) {
        // Same size — just clear to fully transparent.
        std::fill(buf_.begin(), buf_.end(), static_cast<std::uint8_t>(0));
        return;
    }
    if (tex_ != nullptr) {
        SDL_DestroyTexture(tex_);
        tex_ = nullptr;
    }
    w_ = ow;
    h_ = oh;
    buf_.assign(static_cast<std::size_t>(ow) * oh * 4, 0);  // transparent
    tex_ = create_stream_tex(ren, ow, oh);
    if (tex_ != nullptr) {
        SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
    }
}

bool TextOverlay::begin(SDL_Renderer* ren, enhance::HdText& font, int& ow,
                        int& oh) {
    if (ren == nullptr) return false;
    int rw = 0, rh = 0;
    if (SDL_GetRendererOutputSize(ren, &rw, &rh) != 0 || rw <= 0 || rh <= 0) {
        return false;
    }
    ensure(ren, rw, rh);
    if (tex_ == nullptr) return false;
    ow = rw;
    oh = rh;
    font.set_cap_px(cap_px_for(rw));
    return true;
}

void TextOverlay::flush(SDL_Renderer* ren, int logical_w, int logical_h) {
    if (tex_ == nullptr || w_ <= 0 || h_ <= 0) return;
    SDL_UpdateTexture(tex_, nullptr, buf_.data(), w_ * 4);
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
    const int unit_w = std::max({font.measure("888888  x  10"),
                                 font.measure("888888  x  1000"),
                                 font.measure("888888")});
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
