// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/level_surface.hpp"

#include "enhance/upscale.hpp"

namespace olduvai::presentation {

LevelSurface::LevelSurface(SDL_Window* win, SDL_Renderer* ren, bool hd,
                           int hd_scale, const std::string& hd_font,
                           const std::string& hd_profile, LogicalDims initial)
    : win_(win),
      ren_(ren),
      hd_(hd),
      hd_scale_(hd_scale),
      hd_profile_(&hd_profile),
      lsz_(ren, initial.w, initial.h) {
    // Vector text rides the HD substrate.  Only load the font when HD is on;
    // every downstream `use_hd_text()` then falls back to the bitmap path on
    // its own, which is what makes a missing font file a degradation rather
    // than a failure.  Both drivers had this block, character for character
    // apart from which options struct held the font name.
    if (hd_) {
        const std::string base = sdl_base_dir();   // exe dir, or Contents/Resources/
        if (!hd_text_.load(base, hd_scale_, hd_font)) {
            enhance::HdText::report_missing(base, hd_font);
        }
    }
    // The classic streaming texture stays 320*hd_scale wide for EVERY
    // non-widescreen path (loading / tally / transitions / pause / classic
    // present).  Widescreen draws into wide_tex().
    tex_ = create_stream_tex(ren_, 320 * hd_scale_, 200 * hd_scale_);
}

LevelSurface::~LevelSurface() {
    if (tex_ != nullptr) SDL_DestroyTexture(tex_);
    if (wide_tex_ != nullptr) SDL_DestroyTexture(wide_tex_);
}

SDL_Texture* LevelSurface::upload(const std::vector<std::uint8_t>& px,
                                  int native_w, Res res) {
    SDL_Texture* const t = native_w == 320 ? tex_ : wide_tex(native_w);
    const int pitch = native_w * hd_scale_ * 4;
    if (res == Res::kNative && hd_scale_ > 1) {
        const std::vector<std::uint8_t> up = enhance::upscale_rgba(
            px, native_w, 200, hd_scale_, *hd_profile_);
        SDL_UpdateTexture(t, nullptr, up.data(), pitch);
    } else {
        SDL_UpdateTexture(t, nullptr, px.data(), pitch);
    }
    return t;
}

SDL_Texture* LevelSurface::wide_tex(int native_w) {
    if (wide_tex_ == nullptr || native_w != wide_w_) {
        if (wide_tex_ != nullptr) SDL_DestroyTexture(wide_tex_);
        wide_tex_ = create_stream_tex(ren_, native_w * hd_scale_,
                                      200 * hd_scale_);
        wide_w_ = native_w;
    }
    return wide_tex_;
}

}  // namespace olduvai::presentation
