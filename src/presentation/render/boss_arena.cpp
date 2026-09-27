// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/boss_arena.hpp"

#include "presentation/diag/frame_stats.hpp"

#include <algorithm>
#include <cstdlib>   // std::getenv (OLDUVAI_WS_FORCE_MARGIN widescreen override)

#include "enhance/hd_asset_cache.hpp"
#include "enhance/upscale.hpp"
#include "presentation/image_out.hpp"   // present_output
#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/menu/menu_render.hpp"
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

BossWidescreen::BossWidescreen(LevelSurface& surface, bool enabled,
                               LogicalDims fallback)
    : surface_(surface), enabled_(enabled), fallback_(fallback) {
    SDL_GetRendererOutputSize(surface_.ren(), &ow0_, &oh0_);
    M = enabled_ ? boss_ws_margin(ow0_, oh0_,
                                  std::getenv("OLDUVAI_WS_FORCE_MARGIN"))
                 : 0;
    active = enabled_ && M > 0;
    w = 320 + 2 * M;
}

void BossWidescreen::rebuild_if_resized() {
    if (!enabled_) return;
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(surface_.ren(), &ow, &oh);
    if (ow == ow0_ && oh == oh0_) return;   // unchanged
    ow0_ = ow;
    oh0_ = oh;
    M = boss_ws_margin(ow, oh, std::getenv("OLDUVAI_WS_FORCE_MARGIN"));
    w = 320 + 2 * M;
    active = M > 0;

    // Active: the wide canvas fills the output; inactive (margin 0): the
    // aspect_logical pillarbox.  SDL and lsz change together.
    const int s = surface_.hd_scale();
    surface_.lsz().set(active ? w * s : fallback_.w,
                       active ? 200 * s : fallback_.h);
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
    surface_.overlay_pass([&](const enhance::Canvas& cv) {
        hud_.draw_into(cv, draw_lives, /*cx_native=*/0,
                       /*total_native_w=*/320);
    });
}

void BossArenaPresenter::hud_overlay_wide(bool draw_lives) {
    // Centre origin ws_.M, total native width ws_.w (= 320 + 2M).
    surface_.overlay_pass([&](const enhance::Canvas& cv) {
        hud_.draw_into(cv, draw_lives, ws_.M, ws_.w);
    });
}

void BossArenaPresenter::present_frame(bool draw_lives, bool do_present) {
    // Brackets the WHOLE present, matching FramePresenter::present, so the two
    // drivers' present_total columns mean the same thing.
    FrameStats::Timer pt(stats, &FrameStats::present_ms);
    if (stats != nullptr) stats->note_present();
    ws_.rebuild_if_resized();
    SDL_Renderer* const ren = surface_.ren();
    SDL_Texture* const tex = surface_.tex();

    // Bitmap path: unchanged (classic only, draw into 320x200 fb).
    if (!surface_.use_hd_text()) {
        if (draw_lives) hud_.draw_classic_lives(fb_);
    }
    {
        FrameStats::Timer ut(stats, &FrameStats::upload_ms);
        // fb_ is at the surface's scale (HD, or 1 in classic): no upscale.
        surface_.upload(fb_.px, 320, LevelSurface::Res::kHd);
    }
    if (ws_.active) {
        // Non-wide present on a wide canvas (the victory / KO-flash fallback):
        // pillarbox the 320 texture at the centre, HUD with the wide mapping.
        surface_.show_pillarboxed(ws_.M);
        if (surface_.use_hd_text()) hud_overlay_wide(draw_lives);
    } else {
        surface_.show(tex);
        // Vector HUD labels at OUTPUT resolution (crisp at any window scale).
        if (surface_.use_hd_text()) hud_overlay(draw_lives);
    }
    if (do_present) {
        FrameStats::Timer st(stats, &FrameStats::swap_ms);
        present_output(ren);
    }
}

std::vector<std::uint8_t> BossArenaPresenter::build_wide_up() {
    const int hd_scale = surface_.hd_scale();
    const std::string& profile = *surface_.hd_profile();

    // 1. Cached static wide bg: the HUD-clean arena composed wide (pure edge
    // reflection, 0.10 gradient), upscaled once.
    if (bg_hd_M_ != ws_.M || bg_hd_profile_ != profile) {
        FrameBuffer cbg{320, 200};
        std::copy(arena_bg->begin(), arena_bg->end(), cbg.px.begin());
        std::vector<std::uint8_t> wide;
        compose_arena_wide(wide, ws_.M, cbg);
        bg_hd_ = enhance::upscale_rgba(wide, ws_.w, 200, hd_scale, profile);
        bg_hd_M_ = ws_.M;
        bg_hd_profile_ = profile;
    }

    // Timed separately: the background copy (2.5 MB at scale 3, the same for
    // every boss) and the sprite blit (varies by boss).
    std::vector<std::uint8_t> out;
    {
        FrameStats::Timer bt(stats, &FrameStats::bg_copy_ms);
        out = bg_hd_;   // copy cached HD wide bg
    }
    // 2. draw the live fight sprites over the HD buffer at origin_x = M (per-
    //    asset HD cache) so edge-crossing sprites overflow into the margins.
    RenderTarget wrt = boss_visual_target(out.data(), ws_.w * hd_scale,
                                          200 * hd_scale, hd_scale, &cache_,
                                          surface_.hd_profile(), ws_.M);
    apply_smooth(wrt);
    {
        FrameStats::Timer st(stats, &FrameStats::scene_ms);
        draw_fight_sprites(wrt);
    }
    return out;
}

void BossArenaPresenter::show_wide_up(const std::vector<std::uint8_t>& up,
                                      bool draw_lives, bool do_present) {
    FrameStats::Timer pt(stats, &FrameStats::present_ms);
    if (stats != nullptr) stats->note_present();
    SDL_Renderer* const ren = surface_.ren();
    {
        FrameStats::Timer ut(stats, &FrameStats::upload_ms);
        surface_.upload(up, ws_.w, LevelSurface::Res::kHd);
    }
    surface_.show(ws_.wtex());
    // Vector HUD over the center 320 sub-region (mapped into the wide domain).
    if (surface_.hd_text().ok()) hud_overlay_wide(draw_lives);
    if (do_present) {
        FrameStats::Timer st(stats, &FrameStats::swap_ms);
        present_output(ren);
    }
}

std::vector<std::uint8_t> BossArenaPresenter::compose_wide_native(
    const std::function<void(RenderTarget&)>& draw) {
    if (!ws_.active || ws_.wtex() == nullptr || arena_bg == nullptr) return {};
    FrameBuffer cbg{320, 200};
    std::copy(arena_bg->begin(), arena_bg->end(), cbg.px.begin());
    std::vector<std::uint8_t> wide;
    compose_arena_wide(wide, ws_.M, cbg);
    if (draw) {
        RenderTarget wrt = boss_visual_target(wide.data(), ws_.w, 200, 1,
                                              nullptr, nullptr, ws_.M);
        draw(wrt);
    }
    return wide;
}

void BossArenaPresenter::show_wide_native(const std::vector<std::uint8_t>& wide,
                                          bool draw_lives, bool do_present,
                                          bool draw_hud) {
    if (ws_.wtex() == nullptr) return;
    const std::vector<std::uint8_t> up =
        enhance::upscale_rgba(wide, ws_.w, 200, surface_.hd_scale(),
                              *surface_.hd_profile());
    if (draw_hud) {
        show_wide_up(up, draw_lives, do_present);
        return;
    }
    surface_.upload(up, ws_.w, LevelSurface::Res::kHd);
    surface_.show(ws_.wtex());
    if (do_present) present_output(surface_.ren());
}

void BossArenaPresenter::use_wide_logical() {
    surface_.lsz().set(ws_.w * surface_.hd_scale(), 200 * surface_.hd_scale());
}

const FrameBuffer& BossArenaPresenter::last_wide_native() const {
    return ws_.last_native;
}

void BossArenaPresenter::keep_fade_source(const FrameBuffer& nat) {
    ws_.last_native = nat;
}

int BossArenaPresenter::wide_w() const { return ws_.w; }

bool BossArenaPresenter::wide_on() const { return ws_.active; }

bool BossArenaPresenter::wide_ready() const {
    return ws_.active && ws_.wtex() != nullptr;
}

void BossArenaPresenter::rebuild_if_resized() { ws_.rebuild_if_resized(); }

void BossArenaPresenter::present_wide(bool draw_lives, bool do_present) {
    ws_.rebuild_if_resized();

    // A resize may have dropped widescreen (null wtex): use the pillarbox
    // present.
    if (!ws_.active || ws_.wtex() == nullptr) {
        present_frame(draw_lives, do_present);
        return;
    }
    show_wide_up(build_wide_up(), draw_lives, do_present);
}

void BossArenaPresenter::present_wide_native(const FrameBuffer& nat,
                                             bool draw_lives, bool do_present,
                                             bool draw_hud) {
    FrameStats::Timer pt(stats, &FrameStats::present_ms);
    if (stats != nullptr) stats->note_present();
    ws_.rebuild_if_resized();
    ws_.last_native = nat;
    SDL_Renderer* const ren = surface_.ren();
    const int hd_scale = surface_.hd_scale();
    const std::string& profile = *surface_.hd_profile();

    if (!ws_.active || ws_.wtex() == nullptr) {
        const std::vector<std::uint8_t> up =
            enhance::upscale_rgba(nat.px, 320, 200, hd_scale, profile);
        {
            FrameStats::Timer ut(stats, &FrameStats::upload_ms);
            surface_.upload(up, 320, LevelSurface::Res::kHd);
        }
        surface_.show(surface_.tex());
        if (draw_hud && surface_.use_hd_text()) hud_overlay(draw_lives);
        if (do_present) {
            FrameStats::Timer st(stats, &FrameStats::swap_ms);
            present_output(ren);
        }
        return;
    }
    std::vector<std::uint8_t> wide;
    compose_arena_wide(wide, ws_.M, nat);
    const std::vector<std::uint8_t> up =
        enhance::upscale_rgba(wide, ws_.w, 200, hd_scale, profile);
    {
        FrameStats::Timer ut(stats, &FrameStats::upload_ms);
        surface_.upload(up, ws_.w, LevelSurface::Res::kHd);
    }
    surface_.show(ws_.wtex());
    if (draw_hud && surface_.hd_text().ok()) hud_overlay_wide(draw_lives);
    if (do_present) {
        FrameStats::Timer st(stats, &FrameStats::swap_ms);
        present_output(ren);
    }
}

void BossArenaPresenter::present_any(bool draw_lives, bool do_present) {
    const bool l4_victory = ws_.active && wide_victory && wide_victory();
    if (l4_victory) {
        // L4 ride-off: the wide buffer as the fight builds it (clean arena,
        // mirror, 0.10 gradient), then the victory sprites once at origin_x = M
        // so rider and dino overflow into the margins.  320 fallback if a
        // resize dropped widescreen.
        ws_.rebuild_if_resized();

        // Keep the fade source (ws_.last_native) current; by the fade the dino
        // has ridden off, so the native frame is fine.
        FrameBuffer vnat;
        {
            RenderTarget nrt{vnat.px.data(), 320, 200, 1, nullptr, nullptr};
            draw_victory_native(nrt);
        }
        ws_.last_native = vnat;

        if (!ws_.active || ws_.wtex() == nullptr) {   // resize dropped widescreen
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

void BossArenaPresenter::show_native(const FrameBuffer& f) {
    SDL_Renderer* const ren = surface_.ren();
    surface_.upload(f.px, 320, LevelSurface::Res::kNative);
    if (ws_.active)
        surface_.show_pillarboxed(ws_.M);
    else
        surface_.show(surface_.tex());
    present_output(ren);
}

void BossArenaPresenter::capture_shot(const std::string& path,
                                      bool real_output) {
    SDL_Renderer* const ren = surface_.ren();
    const bool text_ok = surface_.hd_text().ok();
    if (real_output) {
        // Rendered without presenting: a post-present readback is black on
        // Metal.
        present_any(true, /*do_present=*/false);
        capture_renderer_output(ren, path);
    } else if (ws_.active) {
        // The wide buffer + HUD at its true size (a readback would capture
        // the window's aspect).
        std::vector<std::uint8_t> up = build_wide_up();
        const int uw = ws_.w * surface_.hd_scale();
        const int uh = 200 * surface_.hd_scale();
        if (text_ok)
            hud_.draw_into({up, uw, uh}, /*draw_lives=*/true,
                           /*cx_native=*/ws_.M, /*total_native_w=*/ws_.w);
        save_rgba_image(up.data(), uw, uh, path);
    } else if (surface_.hd()) {
        // The HUD labels live in the output overlay, not fb.
        surface_.upload(fb_.px, 320, LevelSurface::Res::kHd);
        surface_.show(surface_.tex());
        if (text_ok) hud_overlay(/*draw_lives=*/true);
        capture_renderer_output(ren, path);
    } else {
        save_rgba_image(fb_.px.data(), fb_.w, fb_.h, path);
    }
}

void BossArenaPresenter::apply_smooth(RenderTarget& rt) const {
    boss_smooth_pos(rt, smooth_use_float != nullptr && *smooth_use_float,
                    smooth_fx != nullptr ? *smooth_fx : 0.0f,
                    smooth_fy != nullptr ? *smooth_fy : 0.0f);
}

void BossArenaPresenter::show_pause(
    const std::function<void(RenderTarget&)>& render_frame,
    const std::function<void(RenderTarget&)>& render_sprites,
    const Menu& menu, const ConfirmDialog& confirm,
    const std::vector<formats::Sprite>& charset, const formats::Sprite* bone,
    const std::vector<formats::Rgb>* bone_palette) {
    SDL_Texture* const tex = surface_.tex();
    const bool hd = surface_.hd();
    const int hd_scale = surface_.hd_scale();
    enhance::HdText& text = surface_.hd_text();
    LogicalSize& lsz = surface_.lsz();
    ws_.rebuild_if_resized();   // Alt+Enter taken WHILE paused
    // The menu is laid out in native coordinates: compose at 320x200 and
    // upscale the whole frame into the HD texture, never an SDL stretch
    // (nearest, scale quality "0").
    FrameBuffer pf{320, 200};
    RenderTarget prt{pf.px.data(), 320, 200, 1, nullptr, nullptr};
    // Freeze at the granularity of the last present: a vsync tick can
    // end mid-interpolation, and redrawing at the integer position
    // nudges the player up to hd_scale pixels on ESC.
    apply_smooth(prt);
    render_frame(prt);

    // HD: slab and dim come from draw_menu (upscaled); glyphs and
    // cursor go to the vector overlay.  Classic draws bitmap glyphs
    // into the native buffer.
    const bool menu_use_vector = surface_.use_hd_text();

    // Widescreen: rebuild the frozen arena as the live fight does:
    // mirror the clean background (reflect_pure, 0.10 edge gradient),
    // then draw the sprites once at origin_x = ws_.M.  Mirroring the
    // composed frame would reflect live sprites into the margins.
    FrameBuffer wide_pf;
    bool wide_pause = hd && wide_ready() && wide_w() > 320;
    if (wide_pause) {
        std::vector<std::uint8_t> wbuf =
            compose_wide_native([&](RenderTarget& wrt) {
                // The same three fields the live wide compose feeds, so
                // the paused edge matches it byte for byte.
                apply_smooth(wrt);
                render_sprites(wrt);
            });
        wide_pf = FrameBuffer{wide_w(), 200};
        if (wbuf.size() == wide_pf.px.size()) {
            wide_pf.px = std::move(wbuf);
        } else {
            wide_pause = false;   // give up → pillarbox fallback
        }
    }

    FrameBuffer& menu_fb = wide_pause ? wide_pf : pf;
    if (confirm.is_open()) {
        // The confirm dialog replaces the menu while open.
        draw_confirm(menu_fb, confirm, charset, /*dim=*/true,
                     /*draw_text=*/!menu_use_vector);
    } else if (menu.is_open()) {
        // is_open(): the overlay can be open before its screen is.
        draw_menu(menu_fb, menu, charset, /*dim=*/true,
                  /*draw_text=*/!menu_use_vector, bone,
                  bone_palette);
    }
    // The native pause compose, upscaled into the wide texture or the
    // fight's 320 one.
    surface_.upload(menu_fb.px, wide_pause ? ws_.w : 320,
                    LevelSurface::Res::kNative);
    if (wide_pause)   // already the full wide canvas: no bezel
        surface_.show(ws_.wtex());
    else if (ws_.active)
        surface_.show_pillarboxed(ws_.M);
    else
        surface_.show(tex);

    if (menu_use_vector) {
        surface_.overlay_pass([&](const enhance::Canvas& cv) {
            // Boss HUD in the same overlay pass, before the menu (a
            // second begin/flush would not composite).  Without it the
            // paused frame shows mirrored arena where the HUD belongs.
            hud_.draw_into(cv, /*draw_lives=*/true,
                           /*cx_native=*/ws_.active ? ws_.M : 0,
                           /*total_native_w=*/ws_.active ? ws_.w : 320);
            // The picture's rect in the output (letterboxed;
            // widescreen: the centre 320 at ws_.M), so glyphs land on
            // the slab.
            const MenuFrame pic =
                ws_.active
                    ? MenuFrame::picture(cv.w, cv.h, lsz.w(), lsz.h(),
                                         ws_.M * hd_scale,
                                         320 * hd_scale)
                    : MenuFrame::picture(cv.w, cv.h, lsz.w(), lsz.h());
            if (confirm.is_open())
                draw_confirm_vector(cv, text, confirm, pic);
            else if (menu.is_open())
                draw_menu_vector(cv, text, menu, 0.0f, pic);
        });
    }
}

}  // namespace olduvai::presentation
