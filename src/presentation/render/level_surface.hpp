// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// What a level draws on: the renderer and window (borrowed from the SdlWindow
// that outlives every level), and the owned streaming texture, vector font,
// text overlay and logical size, plus the HD settings.  Shared by both
// drivers.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

#include "enhance/enhanced_hud.hpp"
#include "enhance/hd_asset_cache.hpp"
#include "enhance/hd_text.hpp"
#include "presentation/diag/frame_stats.hpp"
#include "presentation/render/dirty_rects.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/render/logical_size.hpp"
#include "presentation/render/text_overlay.hpp"
#include "presentation/window_util.hpp"   // LogicalDims, show_texture

namespace olduvai::presentation {

class LevelSurface;

// What a full-screen text presenter borrows from its driver
// (sequence/text_screen_present.hpp); LevelSurface::text_screen builds one.
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
    // Uploads go through the surface, which tracks what its textures hold.
    LevelSurface* surface;
};

class LevelSurface {
public:
    // `hd` / `hd_scale` come from the caller (computed before load_level).
    // `initial` logical size: the platform driver starts at 0x0 and lets the
    // widescreen presenter set it; the boss driver starts at its aspect_logical
    // fallback so the loading card is not stretched.
    LevelSurface(SDL_Window* win, SDL_Renderer* ren, bool hd, int hd_scale,
                 const std::string& hd_font, const std::string& hd_profile,
                 LogicalDims initial);
    ~LevelSurface();
    LevelSurface(const LevelSurface&) = delete;
    LevelSurface& operator=(const LevelSurface&) = delete;

    SDL_Renderer* ren() const { return ren_; }
    SDL_Window* win() const { return win_; }
    SDL_Texture* tex() const { return tex_; }
    // The wide canvas's texture, native_w x 200 at the HD scale: made on
    // first use, remade when the width changes.  The level's widescreen
    // presenter and the boss arena size it by their own margin rules.
    SDL_Texture* wide_tex(int native_w);
    enhance::HdText& hd_text() { return hd_text_; }
    TextOverlay& overlay() { return overlay_; }
    LogicalSize& lsz() { return lsz_; }

    bool hd() const { return hd_; }
    int hd_scale() const { return hd_scale_; }
    // HD and the font loaded; a missing font falls back to the bitmap path.
    bool use_hd_text() const { return hd_ && hd_text_.ok(); }
    // The vector HUD's layout for `state`; none without the vector HUD.
    std::optional<enhance::EnhancedHudLayout> hud_layout(
        const systems::SystemsState& state) {
        if (!use_hd_text()) return std::nullopt;
        return enhance::compute_enhanced_hud_layout(hd_text_, state);
    }
    // Live: the address the driver keeps writing (Options change it mid-level).
    const std::string* hd_profile() const { return hd_profile_; }

    // ---- the present every path shares: upload, show, overlay ----
    // Pixels for a native_w x 200 canvas: kNative at 1x (upscaled here by
    // the live profile), or kHd at the surface's scale already.
    enum class Res { kNative, kHd };
    // Into the texture for that width (320: tex(); wider: wide_tex()).
    SDL_Texture* upload(const std::vector<std::uint8_t>& px, int native_w,
                        Res res);
    // An HD frame (native_w x 200 at the surface's scale) from a present that
    // restores only what changed (dirty_rects.hpp): only `rects` of it go up,
    // or all of it when `rects` is null.  The rects are honoured only while
    // the texture still holds `owner`'s last upload; any other write since
    // (upload(), a new texture) makes this a whole upload, so no other path
    // can leave stale pixels on screen.
    void upload_dirty(const std::vector<std::uint8_t>& px, int native_w,
                      const std::vector<DirtyRect>* rects, const void* owner);
    // OLDUVAI_DIRTY=0 turns the dirty path off: every present copies the
    // whole background and uploads the whole frame.
    static bool dirty_on();
    // OLDUVAI_DIRTY_VERIFY=1: each texture's contents are tracked on the CPU
    // from every upload, and verify_dirty compares them with `ref`, the same
    // frame composed whole.  A mismatch is a pixel the dirty path left stale;
    // the counts print when the surface goes.
    bool verifying_dirty() const { return verify_; }
    void verify_dirty(const std::vector<std::uint8_t>& ref, int native_w);
    // FrameStats for the dirty-upload counters; null leaves them off.
    void set_stats(FrameStats* fs) { stats_ = fs; }
    // The output cleared to black and `tex` over the whole canvas.
    void show(SDL_Texture* tex) { show_texture(ren_, tex); }
    // The 320 texture as the centre of a wide canvas, black either side:
    // `margin` native px each side.
    void show_pillarboxed(int margin) {
        const SDL_Rect dst{margin * hd_scale_, 0, 320 * hd_scale_,
                           200 * hd_scale_};
        show_texture(ren_, tex_, &dst);
    }
    // One pass of the output-resolution overlay (TextOverlay::pass), flushed
    // at the logical size.
    template <class Draw>
    void overlay_pass(Draw&& draw,
                      std::uint64_t key = TextOverlay::kAlwaysRedraw) {
        overlay_.pass(ren_, hd_text_, lsz_.w(), lsz_.h(),
                      std::forward<Draw>(draw), key);
    }

    TextScreenDeps text_screen(Uint32 frame_ms) {
        return TextScreenDeps{ren_,      win_,        tex_,
                              &hd_text_, &overlay_,   &lsz_,
                              hd_scale_, hd_profile_, frame_ms,
                              this};
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
    SDL_Texture* wide_tex_ = nullptr;
    int wide_w_ = 0;

    // Per texture: whose upload_dirty it last took (null after any other
    // write) and, under verify, a CPU copy of what it holds.
    struct TexState {
        const void* owner = nullptr;
        std::vector<std::uint8_t> shadow;
        bool shadow_ok = false;
    };
    TexState tex_state_, wide_state_;
    TexState& state_of(SDL_Texture* t) {
        return t == tex_ ? tex_state_ : wide_state_;
    }
    // A whole-texture write: the texture holds `px`, and nobody's rects.
    void note_whole(SDL_Texture* t, const std::uint8_t* px, std::size_t n,
                    const void* owner);
    bool verify_ = false;
    unsigned long verify_checked_ = 0, verify_bad_ = 0, verify_partial_ = 0;
    FrameStats* stats_ = nullptr;
};

// RenderTarget over `b`: the HD per-asset path only for a buffer that is
// actually HD-sized (320 * hd_scale wide).  A native 320-wide scratch buffer in
// HD (loading/tally) takes scale 1 and no cache: it is upscaled whole later.
inline RenderTarget make_render_target(FrameBuffer& b, const LevelSurface& s,
                                       enhance::HdAssetCache& cache) {
    if (s.hd() && b.w == 320 * s.hd_scale()) {
        return RenderTarget{b.px.data(), b.w,   b.h,
                            s.hd_scale(), &cache, s.hd_profile()};
    }
    return RenderTarget{b.px.data(), b.w, b.h, 1, nullptr, nullptr};
}

}  // namespace olduvai::presentation
