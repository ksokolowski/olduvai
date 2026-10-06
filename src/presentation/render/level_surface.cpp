// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/level_surface.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "enhance/upscale.hpp"
#include "presentation/env_num.hpp"

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
    verify_ = env_int("OLDUVAI_DIRTY_VERIFY", 0) != 0;
}

LevelSurface::~LevelSurface() {
    if (verify_)
        std::fprintf(stderr,
                     "dirty-verify: checked=%lu partial=%lu mismatches=%lu\n",
                     verify_checked_, verify_partial_, verify_bad_);
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
        note_whole(t, up.data(), up.size(), nullptr);
    } else {
        SDL_UpdateTexture(t, nullptr, px.data(), pitch);
        note_whole(t, px.data(), px.size(), nullptr);
    }
    return t;
}

bool LevelSurface::dirty_on() {
    static const bool on = env_int("OLDUVAI_DIRTY", 1) != 0;
    return on;
}

void LevelSurface::note_whole(SDL_Texture* t, const std::uint8_t* px,
                              std::size_t n, const void* owner) {
    TexState& s = state_of(t);
    s.owner = owner;
    if (!verify_) return;
    s.shadow.assign(px, px + n);
    s.shadow_ok = true;
}

void LevelSurface::upload_dirty(const std::vector<std::uint8_t>& px,
                                int native_w,
                                const std::vector<DirtyRect>* rects,
                                const void* owner) {
    SDL_Texture* const t = native_w == 320 ? tex_ : wide_tex(native_w);
    const int w = native_w * hd_scale_, h = 200 * hd_scale_;
    const int pitch = w * 4;
    TexState& s = state_of(t);
    if (rects == nullptr || s.owner != owner) {
        SDL_UpdateTexture(t, nullptr, px.data(), pitch);
        note_whole(t, px.data(), px.size(), owner);
        return;
    }
    // Merge slack: a 64x64 block of HD pixels, about what one extra upload
    // call is worth.  OLDUVAI_DIRTY_UPLOAD=bands sends full-width row bands
    // instead (no GLES2 repack, more bytes): the two are A/B'd on the device.
    constexpr long long kMergeSlack = 64 * 64;
    static const bool bands = [] {
        const char* v = std::getenv("OLDUVAI_DIRTY_UPLOAD");
        return v != nullptr && std::strcmp(v, "bands") == 0;
    }();
    std::vector<DirtyRect> up = *rects;
    if (bands) {
        up = to_row_bands(std::move(up), w);
    } else {
        merge_dirty(up, kMergeSlack);
    }
    long long area = 0;
    for (const DirtyRect& r : up) area += r.area();
    // Most of the frame changed: one whole upload beats many rects.
    if (area * 2 > static_cast<long long>(w) * h) {
        SDL_UpdateTexture(t, nullptr, px.data(), pitch);
        note_whole(t, px.data(), px.size(), owner);
        return;
    }
    for (const DirtyRect& r : up) {
        const SDL_Rect sr{r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0};
        SDL_UpdateTexture(
            t, &sr, px.data() + (static_cast<std::size_t>(r.y0) * w + r.x0) * 4,
            pitch);
    }
    if (verify_) {
        ++verify_partial_;
        if (s.shadow_ok) copy_rects(s.shadow.data(), px.data(), w, up);
    }
    if (stats_ != nullptr && stats_->enabled) {
        ++stats_->dirty_partial;
        stats_->dirty_calls += up.size();
        stats_->dirty_mpx += static_cast<double>(area) / 1e6;
    }
}

void LevelSurface::verify_dirty(const std::vector<std::uint8_t>& ref,
                                int native_w) {
    SDL_Texture* const t = native_w == 320 ? tex_ : wide_tex_;
    if (!verify_ || t == nullptr) return;
    const TexState& s = state_of(t);
    if (!s.shadow_ok || s.shadow.size() != ref.size()) return;
    ++verify_checked_;
    if (std::memcmp(s.shadow.data(), ref.data(), ref.size()) == 0) return;
    // Report the first few with where the first stale pixel is.
    if (verify_bad_++ < 5) {
        const int w = native_w * hd_scale_;
        std::size_t i = 0;
        while (s.shadow[i] == ref[i]) ++i;
        std::fprintf(stderr,
                     "dirty-verify: mismatch at present %lu, first stale "
                     "pixel x=%d y=%d\n",
                     verify_checked_, static_cast<int>(i / 4 % w),
                     static_cast<int>(i / 4 / w));
    }
}

SDL_Texture* LevelSurface::wide_tex(int native_w) {
    if (wide_tex_ == nullptr || native_w != wide_w_) {
        if (wide_tex_ != nullptr) SDL_DestroyTexture(wide_tex_);
        wide_tex_ = create_stream_tex(ren_, native_w * hd_scale_,
                                      200 * hd_scale_);
        wide_w_ = native_w;
        wide_state_ = TexState{};   // holds nothing anyone wrote
    }
    return wide_tex_;
}

}  // namespace olduvai::presentation
