// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/boss_arena.hpp"

#include <algorithm>
#include <cstdlib>   // std::getenv (OLDUVAI_WS_FORCE_MARGIN widescreen override)

#include "enhance/hd_asset_cache.hpp"
#include "enhance/upscale.hpp"
#include "presentation/render/boss_hud.hpp"
#include "presentation/render/boss_widescreen.hpp"
#include "presentation/render/text_overlay.hpp"
#include "presentation/render/widescreen.hpp"

namespace olduvai::presentation {

RenderTarget boss_visual_target(std::uint8_t* px, int w, int h, int scale,
                                enhance::HdAssetCache* cache,
                                const std::string* profile, int origin_x) {
    RenderTarget rt{px, w, h, scale, cache, profile};
    rt.origin_x = origin_x;
    rt.advance_state = false;
    return rt;
}

void boss_smooth_pos(RenderTarget& rt, bool use_float, float pfx, float pfy) {
    rt.use_float_pos = use_float;
    rt.player_fx = pfx;
    rt.player_fy = pfy;
}

// ── BossWidescreen ──────────────────────────────────────────────────────────

BossWidescreen::BossWidescreen(SDL_Renderer* ren, bool enabled, int hd_scale,
                               LogicalDims fallback, LogicalSize* lsz)
    : ren_(ren), enabled_(enabled), hd_scale_(hd_scale),
      fallback_(fallback), lsz_(lsz) {
    SDL_GetRendererOutputSize(ren_, &ow0_, &oh0_);
    M = enabled_ ? boss_ws_margin(ow0_, oh0_,
                                  std::getenv("OLDUVAI_WS_FORCE_MARGIN"))
                 : 0;
    active = enabled_ && M > 0;
    w = 320 + 2 * M;
    wtex = active ? create_stream_tex(ren_, w * hd_scale_, 200 * hd_scale_)
                  : nullptr;
}

BossWidescreen::~BossWidescreen() {
    if (wtex != nullptr) SDL_DestroyTexture(wtex);
}

void BossWidescreen::rebuild_if_resized() {
    if (!enabled_) return;
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(ren_, &ow, &oh);
    if (ow == ow0_ && oh == oh0_) return;   // unchanged
    ow0_ = ow;
    oh0_ = oh;
    const int newM =
        boss_ws_margin(ow, oh, std::getenv("OLDUVAI_WS_FORCE_MARGIN"));
    if (newM != M) {
        M = newM;
        w = 320 + 2 * M;
        if (wtex != nullptr) { SDL_DestroyTexture(wtex); wtex = nullptr; }
        if (M > 0)
            wtex = create_stream_tex(ren_, w * hd_scale_, 200 * hd_scale_);
    }
    active = M > 0;

    // Active → wide canvas fills the output; inactive (margin 0) → the
    // aspect_logical fallback (pillarbox).  Keep SDL's logical size and the
    // restore vars in lockstep.
    lsz_->set(active ? w * hd_scale_ : fallback_.w,
              active ? 200 * hd_scale_ : fallback_.h);
}

void compose_arena_wide(std::vector<std::uint8_t>& out, int M,
                        const FrameBuffer& src) {
    compose_widescreen(out, M, src, /*left=*/nullptr, /*right=*/nullptr,
                       MarginFill{/*hud_rows=*/0, /*backdrop=*/nullptr,
                                  /*reflect_pure=*/true,
                                  /*margin_edge_brightness=*/0.10f});
}

// ── BossArenaPresenter ──────────────────────────────────────────────────────

BossArenaPresenter::BossArenaPresenter(LevelSurface& surface,
                                       BossWidescreen& ws, BossHud& hud,
                                       FrameBuffer& fb,
                                       enhance::HdAssetCache& cache)
    : surface_(surface), ws_(ws), hud_(hud), fb_(fb), cache_(cache) {}

void BossArenaPresenter::hud_overlay(bool draw_lives) {
    // Centre origin 0, total native width 320 — the plain 320 domain.
    int ow = 0, oh = 0;
    TextOverlay& overlay = surface_.overlay();
    if (!overlay.begin(surface_.ren(), surface_.hd_text(), ow, oh)) return;
    hud_.draw_into(overlay.buffer(), ow, oh, draw_lives, /*cx_native=*/0,
                   /*total_native_w=*/320);
    overlay.flush(surface_.ren(), surface_.lsz().w(), surface_.lsz().h());
}

void BossArenaPresenter::hud_overlay_wide(bool draw_lives) {
    // Centre origin ws_.M, total native width ws_.w (= 320 + 2M).
    int ow = 0, oh = 0;
    TextOverlay& overlay = surface_.overlay();
    if (!overlay.begin(surface_.ren(), surface_.hd_text(), ow, oh)) return;
    hud_.draw_into(overlay.buffer(), ow, oh, draw_lives, ws_.M, ws_.w);
    overlay.flush(surface_.ren(), surface_.lsz().w(), surface_.lsz().h());
}

void BossArenaPresenter::present_frame(bool draw_lives, bool do_present) {
    ws_.rebuild_if_resized();
    SDL_Renderer* const ren = surface_.ren();
    SDL_Texture* const tex = surface_.tex();
    const bool hd = surface_.hd();
    const int hd_scale = surface_.hd_scale();

    // Bitmap path: unchanged (classic only, draw into 320x200 fb).
    if (!surface_.use_hd_text()) {
        if (draw_lives) hud_.draw_classic_lives(fb_);
    }
    if (hd) {
        // Arena fb is already HD — upload directly (no upscale, no text).
        SDL_UpdateTexture(tex, nullptr, fb_.px.data(), fb_.w * 4);
    } else {
        SDL_UpdateTexture(tex, nullptr, fb_.px.data(), 320 * 4);
    }
    SDL_RenderClear(ren);
    if (ws_.active) {
        // Wide logical canvas active but this is a non-wide present (the
        // victory/KO-flash fallback): pillarbox the 320 texture into the
        // centered dst rect so it keeps its 4:3-ish proportions inside the
        // wide canvas (no horizontal stretch), then draw the HUD with the
        // WIDE mapping so labels/bar sit over the centered 320 region.
        const SDL_Rect dst{ws_.M * hd_scale, 0, 320 * hd_scale, 200 * hd_scale};
        SDL_RenderCopy(ren, tex, nullptr, &dst);
        if (surface_.use_hd_text()) hud_overlay_wide(draw_lives);
    } else {
        SDL_RenderCopy(ren, tex, nullptr, nullptr);

        // Vector HUD labels at OUTPUT resolution (crisp at any window scale).
        if (surface_.use_hd_text()) hud_overlay(draw_lives);
    }
    if (do_present) SDL_RenderPresent(ren);
}

std::vector<std::uint8_t> BossArenaPresenter::build_wide_up() {
    const int hd_scale = surface_.hd_scale();
    const std::string& profile = *surface_.hd_profile();

    // 1. Cached static wide bg: the HUD-clean arena background composed wide
    //    (margins = pure reflection of its edge strips, so the black arena
    //    walls stay black; 0.10 edge-darkening gradient), upscaled ONCE.
    if (bg_hd_M_ != ws_.M || bg_hd_profile_ != profile) {
        FrameBuffer cbg{320, 200};
        std::copy(arena_bg->begin(), arena_bg->end(), cbg.px.begin());
        std::vector<std::uint8_t> wide;
        compose_arena_wide(wide, ws_.M, cbg);
        bg_hd_ = enhance::upscale_rgba(wide, ws_.w, 200, hd_scale, profile);
        bg_hd_M_ = ws_.M;
        bg_hd_profile_ = profile;
    }

    std::vector<std::uint8_t> out = bg_hd_;   // copy cached HD wide bg
    // 2. draw the live fight sprites over the HD buffer at origin_x = M (per-
    //    asset HD cache) so edge-crossing sprites overflow into the margins.
    RenderTarget wrt = boss_visual_target(out.data(), ws_.w * hd_scale,
                                          200 * hd_scale, hd_scale, &cache_,
                                          surface_.hd_profile(), ws_.M);
    boss_smooth_pos(wrt, smooth_use_float != nullptr && *smooth_use_float,
                    smooth_fx != nullptr ? *smooth_fx : 0.0f,
                    smooth_fy != nullptr ? *smooth_fy : 0.0f);
    draw_fight_sprites(wrt);
    return out;
}

void BossArenaPresenter::show_wide_up(const std::vector<std::uint8_t>& up,
                                      bool draw_lives, bool do_present) {
    SDL_Renderer* const ren = surface_.ren();
    SDL_UpdateTexture(ws_.wtex, nullptr, up.data(),
                      ws_.w * surface_.hd_scale() * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, ws_.wtex, nullptr, nullptr);
    // Vector HUD over the center 320 sub-region (mapped into the wide domain).
    if (surface_.hd_text().ok()) hud_overlay_wide(draw_lives);
    if (do_present) SDL_RenderPresent(ren);
}

void BossArenaPresenter::present_wide(bool draw_lives, bool do_present) {
    ws_.rebuild_if_resized();

    // A resize this frame may have dropped widescreen (margin → 0, ws_.wtex
    // freed): fall back to the pillarbox present rather than touch a null
    // texture.
    if (!ws_.active || ws_.wtex == nullptr) {
        present_frame(draw_lives, do_present);
        return;
    }
    show_wide_up(build_wide_up(), draw_lives, do_present);
}

void BossArenaPresenter::present_wide_native(const FrameBuffer& nat,
                                             bool draw_lives, bool do_present,
                                             bool draw_hud) {
    ws_.rebuild_if_resized();
    ws_.last_native = nat;
    SDL_Renderer* const ren = surface_.ren();
    const int hd_scale = surface_.hd_scale();
    const std::string& profile = *surface_.hd_profile();

    if (!ws_.active || ws_.wtex == nullptr) {
        std::vector<std::uint8_t> up =
            enhance::upscale_rgba(nat.px, 320, 200, hd_scale, profile);
        SDL_UpdateTexture(surface_.tex(), nullptr, up.data(),
                          320 * hd_scale * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, surface_.tex(), nullptr, nullptr);
        if (draw_hud && surface_.use_hd_text()) hud_overlay(draw_lives);
        if (do_present) SDL_RenderPresent(ren);
        return;
    }
    std::vector<std::uint8_t> wide;
    compose_arena_wide(wide, ws_.M, nat);
    std::vector<std::uint8_t> up =
        enhance::upscale_rgba(wide, ws_.w, 200, hd_scale, profile);
    SDL_UpdateTexture(ws_.wtex, nullptr, up.data(), ws_.w * hd_scale * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, ws_.wtex, nullptr, nullptr);
    if (draw_hud && surface_.hd_text().ok()) hud_overlay_wide(draw_lives);
    if (do_present) SDL_RenderPresent(ren);
}

void BossArenaPresenter::present_any(bool draw_lives, bool do_present) {
    const bool l4_victory = ws_.active && wide_victory && wide_victory();
    if (l4_victory) {
        // L4 ride-off victory: build the wide buffer the SAME way the fight
        // does — clean arena bg composed wide (mirror + 0.10 edge gradient),
        // then the victory SPRITES drawn ONCE at origin_x = M so the riding
        // triceratops + player OVERFLOW into the margins.  The old path baked
        // the sprites into a 320 frame and then mirrored it
        // (present_wide_native), which duplicated + clipped the ride-off dino
        // at the screen edges.  Defensive 320 fallback if a resize dropped
        // widescreen mid-ride.
        ws_.rebuild_if_resized();

        // Keep the post-victory wide fade source (ws_.last_native) current: it
        // is faded native-wrapped after the loop; by then the dino has ridden
        // off so the baked-native frame is a fine (near-empty) fade source.
        FrameBuffer vnat;
        {
            RenderTarget nrt{vnat.px.data(), 320, 200, 1, nullptr, nullptr};
            draw_victory_native(nrt);
        }
        ws_.last_native = vnat;

        if (!ws_.active || ws_.wtex == nullptr) {   // resize dropped widescreen
            present_wide_native(vnat, draw_lives, do_present);
            return;
        }
        FrameBuffer cbg{320, 200};
        std::copy(arena_bg->begin(), arena_bg->end(), cbg.px.begin());
        std::vector<std::uint8_t> wide;
        compose_arena_wide(wide, ws_.M, cbg);
        RenderTarget wrt = boss_visual_target(wide.data(), ws_.w, 200, 1,
                                              nullptr, nullptr, ws_.M);
        draw_victory_sprites(wrt);
        show_wide_up(enhance::upscale_rgba(wide, ws_.w, 200,
                                           surface_.hd_scale(),
                                           *surface_.hd_profile()),
                     draw_lives, do_present);
    } else if (ws_.active) {
        present_wide(draw_lives, do_present);
    } else {
        present_frame(draw_lives, do_present);
    }
}

}  // namespace olduvai::presentation
