// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/level_surface.hpp"


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
        std::string base = ".";
        if (char* p = SDL_GetBasePath()) {   // exe dir, or Contents/Resources/
            base = p;
            SDL_free(p);
            if (!base.empty() && base.back() == '/') base.pop_back();
        }
        if (!hd_text_.load(base, hd_scale_, hd_font)) {
            enhance::HdText::report_missing(base, hd_font);
        }
    }
    // The classic streaming texture stays 320*hd_scale wide for EVERY
    // non-widescreen path (loading / tally / transitions / pause / classic
    // present).  Widescreen uses the presenter's own WIDE texture.
    tex_ = create_stream_tex(ren_, 320 * hd_scale_, 200 * hd_scale_);
}

LevelSurface::~LevelSurface() {
    if (tex_ != nullptr) SDL_DestroyTexture(tex_);
}

}  // namespace olduvai::presentation
