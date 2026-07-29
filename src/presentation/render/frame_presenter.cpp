// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/frame_presenter.hpp"

#include <SDL.h>

#include <cstdlib>

#include "enhance/enhanced_hud.hpp"          // compute/draw_enhanced_hud_*
#include "enhance/upscale.hpp"               // upscale_rgba
#include "presentation/render/game_render.hpp"      // FrameBuffer
#include "presentation/image_out.hpp"        // capture_renderer_output
#include "presentation/menu/menu_render.hpp"      // draw_menu_vector/confirm_vector
#include "presentation/menu/pause_service.hpp"    // PauseService
#include "presentation/sequence/screens.hpp"          // enhance::HdText
#include "presentation/render/text_overlay.hpp"     // TextOverlay
#include "presentation/render/widescreen_presenter.hpp"  // WidescreenPresenter, HudLayout
#include "systems/player.hpp"                // systems::SystemsState

namespace olduvai::presentation {

void FramePresenter::present(FrameBuffer& f, bool with_hud, bool do_present) {
    // Bind the live context (pointers → the run-loop locals) to the names the
    // pipeline body uses, so the body below is a verbatim move.
    WidescreenPresenter& wsp = *this->wsp;
    SDL_Renderer* const ren = this->ren;
    SDL_Texture* const tex = this->tex;
    enhance::HdText& hd_text = *this->hd_text;
    TextOverlay& text_overlay = *this->text_overlay;
    PauseService& pause = *this->pause;
    systems::SystemsState& state = *this->state;
    const int logical_w = *this->logical_w;
    const int logical_h = *this->logical_h;
    const bool cheat_open = *this->cheat_open;
    std::string& menu_shot_path = *this->menu_shot_path;
    const bool hd = this->hd;
    const bool use_hd_text = this->use_hd_text;
    const int hd_scale = this->hd_scale;
    // Deref the live pointer once per present (a null profile would mean the
    // caller forgot to wire it; fall back to the empty string, which
    // upscale_rgba treats as the native/no-op profile).
    static const std::string kNoProfile;
    const std::string& hd_profile =
        this->hd_profile != nullptr ? *this->hd_profile : kNoProfile;

    wsp.rebuild_if_resized();   // Alt+Enter / resize: recompute wide state
    // Only the NON-text HUD (gauge boxes, food fill, energy pips) goes in the
    // buffer; the vector HUD TEXT is drawn at output res into text_overlay.
    const bool draw_hud_overlay = hd && with_hud && use_hd_text;
    enhance::EnhancedHudLayout hud_layout;
    if (draw_hud_overlay) {
        hud_layout = enhance::compute_enhanced_hud_layout(hd_text, state);
    }
    // A buffer that is ALREADY WIDE (width == wsp.native_w()) carries real
    // margin content the caller composed — the paused frame, wrapped via
    // wsp.wrap_wide_for().  Present it across the FULL canvas instead of
    // pillarboxing a 320 centre into black bars.  HUD bars go in at native
    // scale with the centre offset (the same call the L3 descent uses), then
    // the whole wide buffer is upscaled once into the WIDE texture.
    const bool wide_frame = hd && wsp.active() && wsp.wide_tex() != nullptr &&
                            f.w == wsp.native_w() && f.h == 200;
    if (wide_frame) {
        std::vector<std::uint8_t> wbuf = f.px;
        if (draw_hud_overlay) {
            enhance::draw_enhanced_hud_bars(wbuf, wsp.native_w(), 200, 1,
                                            hud_layout, wsp.margin());
        }
        const std::vector<std::uint8_t> up = enhance::upscale_rgba(
            wbuf, wsp.native_w(), 200, hd_scale, hd_profile);
        SDL_UpdateTexture(wsp.wide_tex(), nullptr, up.data(),
                          wsp.native_w() * hd_scale * 4);
    } else if (hd) {
        if (f.w == 320 * hd_scale) {
            // Already-HD gameplay buffer: no upscale needed.
            if (draw_hud_overlay) {
                enhance::draw_enhanced_hud_bars(f.px, f.w, f.h, hd_scale,
                                                hud_layout);
            }
            SDL_UpdateTexture(tex, nullptr, f.px.data(), f.w * 4);
        } else {
            // Native-320 buffer (loading/tally/PC1): upscale whole-frame.
            std::vector<std::uint8_t> up =
                enhance::upscale_rgba(f.px, 320, 200, hd_scale, hd_profile);
            if (draw_hud_overlay) {
                enhance::draw_enhanced_hud_bars(up, 320 * hd_scale,
                                                200 * hd_scale, hd_scale,
                                                hud_layout);
            }
            SDL_UpdateTexture(tex, nullptr, up.data(), 320 * hd_scale * 4);
        }
    } else {
        // Classic (320x200): draw the cheat picker with the bitmap font into
        // the native buffer (recomposed clean each frame).
        if (cheat_open) draw_cheat_rows_native(f);
        SDL_UpdateTexture(tex, nullptr, f.px.data(), 320 * 4);
    }
    SDL_RenderClear(ren);
    if (wide_frame) {
        // Already the full wide canvas — no bezel, no pillarbox.
        SDL_RenderCopy(ren, wsp.wide_tex(), nullptr, nullptr);
    } else if (wsp.active()) {
        // Widescreen using the 320-wide tex (transitions/loading/tally/pause):
        // pillarbox the centre into the wide canvas with a PURE-BLACK bezel.
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderFillRect(ren, nullptr);
        SDL_Rect dst{wsp.margin() * hd_scale, 0, 320 * hd_scale, 200 * hd_scale};
        SDL_RenderCopy(ren, tex, nullptr, &dst);
    } else {
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
    }
    // One output-resolution vector-text pass shared by the HUD text, the cheat
    // picker, and the pause/confirm menus (a second begin/flush would not
    // composite).  The picker spells its gate out rather than taking
    // use_hd_text; since the per-feature flags collapsed the two are the same
    // expression (hd && hd_text.ok()), so either spelling is correct here.
    const bool show_cheat = cheat_open && hd && hd_text.ok();
    const bool show_menu =
        pause.open() && use_hd_text && !pause.confirm().is_open();
    const bool show_confirm =
        pause.open() && use_hd_text && pause.confirm().is_open();
    // Save whenever the pause overlay is up (classic draws the bitmap menu into
    // the native frame, so the shot must capture that path too).
    const char* show_menu_shot =
        !menu_shot_path.empty() ? menu_shot_path.c_str()
        : (pause.open() ? std::getenv("OLDUVAI_PAUSE_SHOT") : nullptr);
    if (draw_hud_overlay || show_cheat || show_menu || show_confirm) {
        int ow = 0, oh = 0;
        if (text_overlay.begin(ren, hd_text, ow, oh)) {
            auto& b = text_overlay.buffer();
            if (draw_hud_overlay) {
                if (wsp.active()) {
                    // Pillarboxed WS: HUD text uses the wide mapping (matches
                    // wsp.present()); restore the cap for a same-pass menu.
                    const int saved_cap = hd_text.cap_px();
                    wsp.draw_wide_hud_text(b, ow, oh, hud_layout);
                    hd_text.set_cap_px(saved_cap);
                } else {
                    enhance::draw_enhanced_hud_text(b, ow, oh, hd_text,
                                                    hud_layout);
                    draw_enhanced_banners(b, ow, oh);
                }
            }
            if (show_cheat) draw_cheat_rows(b, ow, oh);
            // WS: the pause frame is pillarboxed at the margin — pass that rect
            // so glyphs land on the slab instead of stretching across the bars.
            // The menu/HUD GLYPHS are laid out in 320-native coordinates, so
            // they always map onto the centre-320 region — for the pillarbox
            // frame AND for a wide_frame (whose slab, laid out in native_w
            // space, is centred on the same point at the same scale).  Mapping
            // a wide_frame's glyphs across the full canvas instead would draw
            // them at native_w/320 times the slab's scale.
            int mfx = -1, mfy = -1, mfw = -1, mfh = -1;
            if (wsp.active()) {
                mfx = wsp.margin() * hd_scale * ow / logical_w;
                mfw = 320 * hd_scale * ow / logical_w;
                mfy = 0;
                mfh = oh;
            }
            if (show_menu)
                draw_menu_vector(b, ow, oh, hd_text, pause.menu(), 0.0f, mfx,
                                 mfy, mfw, mfh);
            if (show_confirm)
                draw_confirm_vector(b, ow, oh, hd_text, pause.confirm(), mfx,
                                    mfy, mfw, mfh);
            text_overlay.flush(ren, logical_w, logical_h);
        }
    }
    if (show_menu_shot) {
        // Read back the fully-composited frame (scene + slab + vector text)
        // right before present to verify the HD overlay.
        capture_renderer_output(ren, show_menu_shot);
        menu_shot_path.clear();   // consume a menu-script `shot` request
    }
    // do_present=false leaves the composited frame in the backbuffer for a
    // caller-side RenderReadPixels (Metal reads black AFTER present).
    if (do_present) SDL_RenderPresent(ren);
}

}  // namespace olduvai::presentation
