// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "prepare/game_files.hpp"
#include "presentation/window_util.hpp"
#include "presentation/sequence/end_sequence.hpp"

#include <SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "enhance/upscale.hpp"
#include "formats/cur.hpp"
#include "formats/mat.hpp"
#include "formats/mdi.hpp"
#include "formats/pc1.hpp"
#include "presentation/render/game_render.hpp"    // FrameBuffer, blit_sprite
#include "presentation/image_out.hpp"      // capture_renderer_output
#include "presentation/render/smooth_present.hpp" // smooth_try_enable_vsync

namespace olduvai::presentation {
namespace {


}  // namespace

void show_game_over_screen(const std::filesystem::path& game_dir,
                           SdlAudio& audio, ScaledWindow& sw, int hd_scale,
                           const std::string& hd_profile) {
    formats::CurArchive eva(prepare::slurp_file(game_dir / "FILESA.VGA"));
    formats::CurArchive efa(prepare::slurp_file(game_dir / "FILESA.CUR"));
    if (!eva.contains("THEEND.PC1")) {
        std::fprintf(stderr, "game-over: THEEND.PC1 not in FILESA.VGA\n");
        audio.stop_music();
        return;
    }
    const formats::Pc1Image end = formats::parse_pc1(eva.get("THEEND.PC1").data);
    if (end.width != 320) {
        std::fprintf(stderr, "game-over: THEEND.PC1 unexpected width\n");
        audio.stop_music();
        return;
    }
    SDL_Texture* gtex =
        create_stream_tex(sw.ren, 320 * hd_scale, 200 * hd_scale);
    // MORT.MDI death music over the picture (no celebratory chime).
    if (audio.music_available()) {
        const std::vector<std::uint8_t>* md = nullptr;
        if (efa.contains("MORT.MDI")) md = &efa.get("MORT.MDI").data;
        if (md != nullptr) {
            audio.play_music(*md, formats::mdi_track_id("mort.mdi"));
        }
    }
    // Compose THEEND.PC1 → RGBA once.
    FrameBuffer end_fb;
    for (std::size_t i = 0; i < end.pixels.size() && i < 320u * 200u; ++i) {
        const std::uint8_t idx = end.pixels[i];
        const formats::Rgb c =
            (idx < end.palette.size()) ? end.palette[idx] : formats::Rgb{};
        end_fb.px[i * 4] = c.r;
        end_fb.px[i * 4 + 1] = c.g;
        end_fb.px[i * 4 + 2] = c.b;
        end_fb.px[i * 4 + 3] = 255;
    }
    upload_native_frame(gtex, end_fb, hd_scale, hd_profile);
    // Hold ~8 seconds (8*18 frames @ 18 Hz), re-presenting each tick and polling
    // QUIT/ESC to abort early.
    constexpr int kHoldFrames = 8 * 18;
    for (int f = 0; f < kHoldFrames; ++f) {
        SDL_Event ev;
        bool abort = false;
        while (SDL_PollEvent(&ev)) {
            if (handle_fullscreen_toggle(ev, sw.win)) continue;
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_KEYDOWN &&
                 ev.key.keysym.sym == SDLK_ESCAPE)) {
                abort = true;
                break;
            }
        }
        if (abort) break;
        SDL_RenderClear(sw.ren);
        SDL_RenderCopy(sw.ren, gtex, nullptr, nullptr);
        SDL_RenderPresent(sw.ren);
        SDL_Delay(1000 / 18);
    }
    // FADE the death music out — do NOT hard-cut it (EXE FUN_2bd7_02e7 ends with
    // MDI_FadeStop; a hard stop chops MORT.MDI mid-loop, audibly wrong).
    audio.fade_out_music();
    SDL_DestroyTexture(gtex);
}

void show_win_ending(const std::filesystem::path& game_dir, SdlAudio& audio,
                     ScaledWindow& sw, int hd_scale,
                     const std::string& hd_profile, bool smooth_motion,
                     bool& quit_requested) {
    formats::CurArchive eva(prepare::slurp_file(game_dir / "FILESA.VGA"));
    formats::CurArchive efa(prepare::slurp_file(game_dir / "FILESA.CUR"));
    SDL_Texture* etex =
        create_stream_tex(sw.ren, 320 * hd_scale, 200 * hd_scale);
    if (audio.music_available() && efa.contains("FIN.MDI")) {
        audio.play_music(efa.get("FIN.MDI").data,
                         formats::mdi_track_id("fin.mdi"));
    }
    // Game_WinSequence (FUN_2bd7_0183): COOL3.PC1 family cave scene + COOL2.MAT
    // caveman rising y=198→73 at -2/frame; smooth-motion interpolates the
    // scroll.  Missing assets → silent return (§F6).  IIFE = structured cleanup.
    [&]() {
        if (!eva.contains("COOL3.PC1") || !eva.contains("COOL2.MAT")) {
            std::fprintf(stderr, "ending: assets missing (COOL3.PC1 / "
                                 "COOL2.MAT not in FILESA.VGA)\n");
            audio.stop_music();
            return;
        }
        const formats::Pc1Image bg =
            formats::parse_pc1(eva.get("COOL3.PC1").data);
        if (bg.width != 320) {
            std::fprintf(stderr, "ending: COOL3.PC1 unexpected width\n");
            audio.stop_music();
            return;
        }
        const std::vector<formats::Sprite> mat_sprites =
            formats::MatFile(eva.get("COOL2.MAT").data, "COOL2.MAT").sprites();
        if (mat_sprites.empty()) {
            std::fprintf(stderr, "ending: COOL2.MAT has no sprites\n");
            audio.stop_music();
            return;
        }
        const formats::Sprite& sprite = mat_sprites[0];

        // Build the RGBA background once.
        FrameBuffer bg_fb;
        for (std::size_t i = 0; i < bg.pixels.size() && i < 320u * 200u; ++i) {
            const std::uint8_t idx = bg.pixels[i];
            const formats::Rgb c =
                (idx < bg.palette.size()) ? bg.palette[idx] : formats::Rgb{};
            bg_fb.px[i * 4] = c.r;
            bg_fb.px[i * 4 + 1] = c.g;
            bg_fb.px[i * 4 + 2] = c.b;
            bg_fb.px[i * 4 + 3] = 255;
        }
        const std::vector<formats::Rgb> pal(bg.palette.begin(),
                                            bg.palette.end());
        // OLDUVAI_ENDING_SHOT: dump the first composited frame via readback,
        // then quit (the OLDUVAI_MAINMENU_SHOT headless-verify pattern).
        const char* const ending_shot = std::getenv("OLDUVAI_ENDING_SHOT");
        auto render_at = [&](int render_y, Uint32 delay_ms) -> bool {
            FrameBuffer fb2 = bg_fb;   // copy background
            blit_sprite(fb2, sprite, pal, 64, render_y);
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (handle_fullscreen_toggle(ev, sw.win)) continue;
                if (ev.type == SDL_QUIT) return false;
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_ESCAPE)
                    return false;
            }
            upload_native_frame(etex, fb2, hd_scale, hd_profile);
            SDL_RenderClear(sw.ren);
            SDL_RenderCopy(sw.ren, etex, nullptr, nullptr);
            if (ending_shot) {
                capture_renderer_output(sw.ren, ending_shot);
                quit_requested = true;
                return false;
            }
            SDL_RenderPresent(sw.ren);
            if (delay_ms > 0) SDL_Delay(delay_ms);
            return true;
        };

        // EXE 0x0207-0x0279: y = 198..73 (loop exits when y < 73).
        constexpr int kYStart = 198;
        constexpr int kYEnd = 73;
        constexpr int kDY = 2;
        constexpr Uint32 kFrameMs = 1000 / 18;   // 18 Hz logic cadence

        const bool smooth = smooth_motion;
        // vsync render-interpolation for the credits scroll (smooth_present.hpp);
        // render_at(y, 0) presents with no extra delay so the vsync block alone
        // paces it.  Without vsync, fall back to 3 discrete sub-frames.
        const bool e_vsync = smooth_try_enable_vsync(sw.ren, smooth);
        bool aborted = false;
        int prev_y = kYStart;
        for (int y = kYStart; y >= kYEnd && !aborted; y -= kDY) {
            if (smooth && e_vsync) {
                const Uint32 t0 = SDL_GetTicks();
                while (!aborted) {
                    const Uint32 el = SDL_GetTicks() - t0;
                    const float a = el >= kFrameMs
                                        ? 1.0f
                                        : static_cast<float>(el) /
                                              static_cast<float>(kFrameMs);
                    const int ly =
                        prev_y +
                        static_cast<int>(std::lround((y - prev_y) * a));
                    aborted = !render_at(ly, 0);
                    if (SDL_GetTicks() - t0 >= kFrameMs) break;
                }
            } else if (smooth) {
                constexpr Uint32 kSubMs = kFrameMs / 3;
                for (int sub = 1; sub <= 3 && !aborted; ++sub) {
                    const int lerp_y = prev_y + (y - prev_y) * sub / 3;
                    aborted = !render_at(lerp_y, kSubMs);
                }
            } else {
                aborted = !render_at(y, kFrameMs);
            }
            prev_y = y;
        }

        if (aborted) {
            audio.stop_music();
            return;
        }

        // Hold the final frame: wait for any key press+release (EXE 0x02a7
        // Keyboard_WaitPressRelease); ESC / QUIT abort immediately.
        bool pressed = false;
        bool done = false;
        while (!done) {
            SDL_Event ev;
            if (SDL_WaitEvent(&ev)) {
                if (ev.type == SDL_QUIT) {
                    done = true;
                    break;
                }
                if (handle_fullscreen_toggle(ev, sw.win)) continue;
                if (ev.type == SDL_KEYDOWN) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        done = true;
                        break;
                    }
                    pressed = true;
                }
                if (ev.type == SDL_KEYUP && pressed) {
                    done = true;
                }
            }
        }
        audio.stop_music();   // EXE 0x02d2: MDI_FadeStop + MDI_FreeSlot(0)
    }();
    SDL_DestroyTexture(etex);
}

}  // namespace olduvai::presentation
