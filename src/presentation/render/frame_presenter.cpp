// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/frame_presenter.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <optional>

#include "enhance/enhanced_hud.hpp"          // compute/draw_enhanced_hud_*
#include "enhance/upscale.hpp"               // upscale_rgba
#include "presentation/diag/frame_stats.hpp" // FrameStats / note_present
#include "presentation/render/game_render.hpp"      // FrameBuffer
#include "presentation/image_out.hpp"        // capture_renderer_output
#include "presentation/render/hud_render.hpp"  // draw_hud        // capture_renderer_output
#include "presentation/menu/menu_render.hpp"      // draw_menu_vector/confirm_vector
#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/sequence/screens.hpp"          // enhance::HdText
#include "presentation/render/text_overlay.hpp"     // TextOverlay
#include "presentation/render/widescreen_presenter.hpp"  // WidescreenPresenter, HudLayout
#include "systems/player.hpp"                // systems::SystemsState

namespace olduvai::presentation {

// The HD-aware HUD compose; see the header.
void FramePresenter::draw_hud_for(FrameBuffer& target) {
    systems::SystemsState& state = *this->state;
    const bool hd = surface->hd();
    const bool with_hd_text = surface->use_hd_text();
    if (!hd) {
        // Classic: bitmap HUD directly onto the 320x200 buffer.
        draw_hud(target, state, *charset, render->entity_sprites,
                 render->palette, false);
        return;
    }

    // HD: run draw_hud on a native scratch for its state changes only (food
    // cap, GET READY decrement); the pixels are discarded.  GET READY
    // visibility is read first: draw_hud draws, then decrements, so the banner
    // shows even on the tick the counter reaches 1.
    const bool get_ready_visible =
        (state.get_ready_counter >= 2 && state.get_ready_counter <= 17);
    // Reset scratch alpha so blit_sprite writes are visible (just in case).
    for (std::size_t i = 3; i < hud_scratch_.px.size(); i += 4)
        hud_scratch_.px[i] = 255;
    draw_hud(hud_scratch_, state, *charset, render->entity_sprites,
             render->palette, with_hd_text);
    // Redraw GET READY at HD.  With vector banners the sprites are skipped and
    // the overlay draws the text instead (which also survives the widescreen
    // re-compose).
    if (get_ready_visible && !with_hd_text) {
        auto rt = make_render_target(target, *surface, *hd_cache);
        if (132 < static_cast<int>(render->entity_sprites.size()))
            blit_sprite(rt, render->entity_sprites[132], render->palette,
                        0x80, 0x64);
        if (133 < static_cast<int>(render->entity_sprites.size()))
            blit_sprite(rt, render->entity_sprites[133], render->palette,
                        0xA2, 0x61);
    }
}

void FramePresenter::present(FrameBuffer& f, bool with_hud, bool do_present) {
    FrameStats::Timer pt(stats, &FrameStats::present_ms);
    if (stats != nullptr) stats->note_present();
    wsp->sync_output();   // Alt+Enter, resize or a live Aspect edit
    // Only the NON-text HUD (gauge boxes, food fill, energy pips) goes in the
    // buffer; the vector HUD TEXT is drawn at output res by draw_text_pass.
    const auto hud =
        with_hud ? surface->hud_layout(*state) : std::nullopt;
    const enhance::EnhancedHudLayout* const hud_p = hud ? &*hud : nullptr;
    finish(upload(f, hud_p), hud_p, do_present);
}

void FramePresenter::present_steady(FrameBuffer& f,
                                    std::vector<BlitRecord>& blits,
                                    std::uint64_t bg_key) {
    FrameStats::Timer pt(stats, &FrameStats::present_ms);
    if (stats != nullptr) stats->note_present();
    wsp->sync_output();
    const auto hud = surface->hud_layout(*state);
    const enhance::EnhancedHudLayout* const hud_p = hud ? &*hud : nullptr;
    const int s = surface->hd_scale();
    // The bars are opaque and drawn last: their boxes are their rects.
    hud_rects_.clear();
    if (hud_p != nullptr) {
        enhance::draw_enhanced_hud_bars(f.canvas(), s, *hud_p);
        add_hud_rects(hud_rects_, *hud_p, s, 0, f.w, f.h);
    }
    const std::vector<DirtyRect>* region =
        LevelSurface::dirty_on() && steady_diff_.ready(bg_key)
            ? &steady_diff_.region(blits, hud_rects_)
            : nullptr;
    {
        FrameStats::Timer ut(stats, &FrameStats::upload_ms);
        surface->upload_dirty(f.px, 320, region, this);
    }
    steady_diff_.commit(blits, hud_rects_, bg_key, /*allow=*/true);
    if (surface->verifying_dirty()) surface->verify_dirty(f.px, 320);
    finish(false, hud_p, /*do_present=*/true);
}

void FramePresenter::finish(bool wide_frame,
                            const enhance::EnhancedHudLayout* hud_p,
                            bool do_present) {
    show_canvas(wide_frame);
    draw_text_pass(hud_p);
    // Pause shots: the fully-composited frame (scene + slab + vector text),
    // whenever the overlay is up, classic included (its bitmap menu is in the
    // native frame).  A menu-script `shot` request is consumed.
    const char* shot =
        !menu_shot_path->empty() ? menu_shot_path->c_str() : pause_shot_;
    if (shot != nullptr) {
        capture_renderer_output(surface->ren(), shot);
        menu_shot_path->clear();
    }
    // do_present=false leaves the composited frame in the backbuffer for a
    // caller-side RenderReadPixels (Metal reads black AFTER present).
    if (do_present) {
        FrameStats::Timer sw(stats, &FrameStats::swap_ms);   // the vsync block
        present_output(surface->ren());
    }
}

bool FramePresenter::upload(FrameBuffer& f,
                            const enhance::EnhancedHudLayout* hud) const {
    using Res = LevelSurface::Res;
    const int s = surface->hd_scale();
    // A null profile falls back to "" (the native / no-op profile).
    static const std::string kNoProfile;
    const std::string& profile =
        surface->hd_profile() != nullptr ? *surface->hd_profile() : kNoProfile;
    if (!surface->hd()) {
        // Classic: the cheat picker in the bitmap font, into the native
        // buffer (recomposed clean each frame).
        if (cheats->open()) draw_cheat_rows_native(f);
        FrameStats::Timer ut(stats, &FrameStats::upload_ms);
        surface->upload(f.px, 320, Res::kHd);   // scale 1
        return false;
    }
    // A buffer already wide (the paused frame from wrap_wide_for) carries
    // real margins: the whole canvas, upscaled once into the wide texture,
    // the HUD bars at HD over the centre, as the steady frame draws them.
    const int wn = wsp->native_w();
    if (wsp->active() && wsp->wide_tex() != nullptr && f.w == wn && f.h == 200) {
        std::vector<std::uint8_t> up =
            enhance::upscale_rgba(f.px, wn, 200, s, profile);
        if (hud != nullptr)
            enhance::draw_enhanced_hud_bars({up, wn * s, 200 * s}, s, *hud,
                                            wsp->margin());
        FrameStats::Timer ut(stats, &FrameStats::upload_ms);
        surface->upload(up, wn, Res::kHd);
        return true;
    }
    if (f.w == 320 * s) {   // an HD gameplay buffer: no upscale
        if (hud != nullptr)
            enhance::draw_enhanced_hud_bars(f.canvas(), s, *hud);
        FrameStats::Timer ut(stats, &FrameStats::upload_ms);
        surface->upload(f.px, 320, Res::kHd);
        return false;
    }
    // A native 320 buffer (loading, tally, PC1): upscaled whole.
    std::vector<std::uint8_t> up =
        enhance::upscale_rgba(f.px, 320, 200, s, profile);
    if (hud != nullptr)
        enhance::draw_enhanced_hud_bars({up, 320 * s, 200 * s}, s, *hud);
    FrameStats::Timer ut(stats, &FrameStats::upload_ms);
    surface->upload(up, 320, Res::kHd);
    return false;
}

void FramePresenter::show_canvas(bool wide_frame) const {
    if (wide_frame)
        surface->show(wsp->wide_tex());
    else if (wsp->active())   // the 320 texture (transitions, loading, tally,
        surface->show_pillarboxed(wsp->margin());   // pause) on black
    else
        surface->show(surface->tex());
}

void FramePresenter::draw_text_pass(const enhance::EnhancedHudLayout* hud) {
    enhance::HdText& text = surface->hd_text();
    const bool vector = surface->use_hd_text();
    const bool show_cheat = cheats->open() && surface->hd() && text.ok();
    // menu().is_open() too: the overlay flag is set before the screen opens,
    // and a closed menu has no current screen.
    const bool show_menu = menu_ != nullptr && vector &&
                           !confirm_->is_open() && menu_->is_open();
    const bool show_confirm = menu_ != nullptr && vector && confirm_->is_open();
    if (hud == nullptr && !show_cheat && !show_menu && !show_confirm) return;
    surface->overlay_pass([&](const enhance::Canvas& cv) {
        const int ow = cv.w, oh = cv.h;
        const int lw = surface->lsz().w(), lh = surface->lsz().h();
        const int s = surface->hd_scale();
        // The picture's rect in the output (letterboxed; widescreen: the
        // centre 320).  Menu glyphs are laid out in 320-native coordinates and
        // map onto it for both the pillarboxed and the wide frame.
        const MenuFrame pic =
            wsp->active() ? MenuFrame::picture(ow, oh, lw, lh,
                                               wsp->margin() * s, 320 * s)
                          : MenuFrame::picture(ow, oh, lw, lh);
        if (hud != nullptr) {
            const int saved_cap = text.cap_px();
            if (wsp->active()) {
                // The wide mapping, as wsp->present() draws it.
                wsp->draw_wide_hud_text(cv, *hud);
            } else {
                // Sized for the picture, not the window.
                text.set_cap_px(std::max(1, TextOverlay::cap_px_for(pic.w)));
                enhance::draw_enhanced_hud_text(cv, text, *hud,
                                                {pic.x, pic.y, pic.w, pic.h});
            }
            text.set_cap_px(saved_cap);   // for a same-pass menu
            if (!wsp->active()) draw_enhanced_banners(cv);
        }
        if (show_cheat) draw_cheat_rows(cv);
        if (show_menu) draw_menu_vector(cv, text, *menu_, 0.0f, pic);
        if (show_confirm) draw_confirm_vector(cv, text, *confirm_, pic);
    });
}

void FramePresenter::present_paused(const Menu& menu,
                                    const ConfirmDialog& confirm,
                                    const char* shot) {
    wsp->sync_output();   // an Aspect edit this frame decides `wide` below
    // The frozen scene at native 320x200 (any --hd-profile), dimmed behind
    // the slab.  advance_state=false: the pause skips the tick, so an
    // advancing compose would drain club_flag once per paused frame.
    FrameBuffer pf{320, 200};
    {
        RenderTarget prt{pf.px.data(), pf.w, pf.h, 1, nullptr, nullptr};
        prt.advance_state = false;
        compose_frame(prt, *state, *render, /*draw_player=*/true);
    }
    // Widescreen: wrap the frozen centre with the live frame's margins, gated
    // on present_path() so the paused frame is never wider than the live one.
    FrameBuffer wide_pf;
    const bool wide = wsp->present_path() && wsp->native_w() > 320;
    if (wide) {
        std::vector<std::uint8_t> wbuf;
        wsp->wrap_wide_for(pf, /*is_present=*/true, wbuf);
        wide_pf = FrameBuffer{wsp->native_w(), 200};
        if (wbuf.size() == wide_pf.px.size()) wide_pf.px = std::move(wbuf);
    }
    FrameBuffer& menu_fb = wide ? wide_pf : pf;
    const bool bitmap_text = !surface->use_hd_text();
    if (confirm.is_open()) {
        // The confirm dialog replaces the menu while open.
        draw_confirm(menu_fb, confirm, *charset, /*dim=*/true, bitmap_text);
    } else if (menu.is_open()) {
        // is_open(): the pause opens before its screen does (esc_pressed, the
        // reinit test hook), and a closed menu has no screen.  HD: slab and
        // accent only, the text pass draws the glyphs.
        const auto& spr = render->entity_sprites;
        draw_menu(menu_fb, menu, *charset, /*dim=*/true, bitmap_text,
                  spr.size() > 33 ? &spr[33] : nullptr, &render->palette);
    }
    menu_ = &menu;
    confirm_ = &confirm;
    pause_shot_ = shot;
    // with_hud: the pause skips the tick's HUD draw; the HD HUD path here is
    // read-only (no second food-cap write or GET READY decrement).
    present(menu_fb, /*with_hud=*/true, /*do_present=*/true);
    menu_ = nullptr;
    confirm_ = nullptr;
    pause_shot_ = nullptr;
}

}  // namespace olduvai::presentation
