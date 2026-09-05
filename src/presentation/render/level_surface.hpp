// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// LevelSurface — what a level draws ON: the renderer and window it borrows,
// the streaming texture, the vector font, the text overlay, the logical size,
// and the HD settings that decide how all of those behave.
//
// BACKLOG §3.7 cluster 1.  Both drivers declared these nine things separately
// and both set them up the same way, including a twelve-line font load with
// four branches that differed only in which options struct held `hd_font`.
// This is not a new abstraction — `TextScreenDeps` below was already exactly
// this list, discovered while unifying the tally presenter in §3.14a and
// serving four call sites across both drivers before anyone named it.  The
// class is that struct grown up enough to own the lifetimes too.
//
// It owns the texture (and destroys it), the font, the overlay and the logical
// size.  It BORROWS the renderer and window, which belong to the SdlWindow that
// outlives every level.
#pragma once

#include <string>

#include <SDL.h>

#include "enhance/hd_asset_cache.hpp"
#include "enhance/hd_text.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/render/logical_size.hpp"
#include "presentation/render/text_overlay.hpp"
#include "presentation/window_util.hpp"   // LogicalDims, create_stream_tex

namespace olduvai::presentation {

// What a full-screen text presenter borrows from its driver.  Kept as a plain
// struct because that is how the presenter takes it; LevelSurface::text_screen
// builds one.  See sequence/text_screen_present.hpp for the presenter itself.
struct TextScreenDeps {
    SDL_Renderer* ren;
    SDL_Window* win;
    SDL_Texture* tex;
    enhance::HdText* hd_text;
    TextOverlay* overlay;
    const LogicalSize* lsz;
    int hd_scale;
    const std::string* hd_profile;   // HD upscale profile name
    Uint32 frame_ms;
};

class LevelSurface {
public:
    // `hd` and `hd_scale` are the caller's, because both drivers compute them
    // before load_level so bind-time decisions can key on the full vector-HUD
    // gate.  `initial` is the starting logical size: the platform driver opens
    // at 0x0 and lets the widescreen presenter set it, the boss driver opens at
    // its aspect_logical fallback so the 320-wide loading card is not stretched
    // across a wide canvas.
    LevelSurface(SDL_Window* win, SDL_Renderer* ren, bool hd, int hd_scale,
                 const std::string& hd_font, const std::string& hd_profile,
                 LogicalDims initial);
    ~LevelSurface();
    LevelSurface(const LevelSurface&) = delete;
    LevelSurface& operator=(const LevelSurface&) = delete;

    SDL_Renderer* ren() const { return ren_; }
    SDL_Window* win() const { return win_; }
    SDL_Texture* tex() const { return tex_; }
    enhance::HdText& hd_text() { return hd_text_; }
    TextOverlay& overlay() { return overlay_; }
    LogicalSize& lsz() { return lsz_; }

    bool hd() const { return hd_; }
    int hd_scale() const { return hd_scale_; }
    // hd && the font actually loaded.  A missing font file is a degradation to
    // the bitmap path, not a failure, so every vector-text site gates on this.
    bool use_hd_text() const { return hd_ && hd_text_.ok(); }
    // Live: Options can change the profile mid-level, so this is the ADDRESS
    // the driver keeps writing, not a copy.
    const std::string* hd_profile() const { return hd_profile_; }

    TextScreenDeps text_screen(Uint32 frame_ms) {
        return TextScreenDeps{ren_,      win_,        tex_,
                              &hd_text_, &overlay_,   &lsz_,
                              hd_scale_, hd_profile_, frame_ms};
    }

private:
    SDL_Window* win_;
    SDL_Renderer* ren_;
    bool hd_;
    int hd_scale_;
    const std::string* hd_profile_;
    enhance::HdText hd_text_;
    TextOverlay overlay_;
    LogicalSize lsz_;
    SDL_Texture* tex_ = nullptr;
};

// Build a RenderTarget over `b`, choosing the HD per-asset path or the native
// scale-1 path by the buffer's ACTUAL width.
//
// Both drivers had this as a prologue lambda — `make_rt` in game_app,
// `make_target` in boss_app — with DIFFERENT predicates: `hd && b.w == 320 *
// hd_scale` against a bare `hd`.  The careful one wins, and it is provably
// equivalent at every boss call site rather than merely gate-verified: boss
// passes exactly one buffer, its arena `fb`, sized `hd ? 320 * hd_scale : 320`.
// In HD both predicates are true; in classic `hd` is false and both are false.
// (Same species of argument as the `hd` / `hd_scale > 1` equivalence the
// 2026-07-24 dedup audit established for upload_native_frame.)
//
// Why the width test is the careful one: a NATIVE 320-wide buffer in HD mode —
// the loading/tally scratch buffers — must take scale 1 and no cache so blits
// land at native coordinates, because it is upscaled whole-frame later.  Only a
// genuinely HD-sized buffer drives the per-asset cache path.
inline RenderTarget make_render_target(FrameBuffer& b, const LevelSurface& s,
                                       enhance::HdAssetCache& cache) {
    if (s.hd() && b.w == 320 * s.hd_scale()) {
        return RenderTarget{b.px.data(), b.w,   b.h,
                            s.hd_scale(), &cache, s.hd_profile()};
    }
    return RenderTarget{b.px.data(), b.w, b.h, 1, nullptr, nullptr};
}

}  // namespace olduvai::presentation
