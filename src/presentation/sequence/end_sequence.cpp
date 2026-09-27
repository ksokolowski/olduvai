// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "prepare/game_files.hpp"
#include "presentation/window_util.hpp"
#include "presentation/sequence/end_sequence.hpp"

#include <SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
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
#include "presentation/env_num.hpp"         // env_int — ENDING_SHOT_FRAME
#include "presentation/image_out.hpp"      // capture_renderer_output, present_output
#include "presentation/render/smooth_present.hpp" // smooth_try_enable_vsync

namespace olduvai::presentation {

void show_game_over_screen(const std::filesystem::path& game_dir,
                           SdlAudio& audio, ScaledWindow& sw, int hd_scale,
                           const std::string& hd_profile) {
    const formats::CurArchive eva(prepare::slurp_file(game_dir / "FILESA.VGA"));
    const formats::CurArchive efa(prepare::slurp_file(game_dir / "FILESA.CUR"));
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
    upload_native_frame(gtex, pc1_frame(end), hd_scale, hd_profile);
    // OLDUVAI_GAMEOVER_SHOT=<path>: capture one presented frame (readback
    // through the live presenter) and skip the 8-second hold and music fade.
    if (const char* shot = std::getenv("OLDUVAI_GAMEOVER_SHOT")) {
        show_texture(sw.ren, gtex);
        present_output(sw.ren);
        capture_renderer_output(sw.ren, shot);
        audio.stop_music();
        SDL_DestroyTexture(gtex);
        return;
    }
    // Hold ~8 seconds (8*18 frames @ 18 Hz), re-presenting each tick and
    // polling QUIT/ESC to abort early.
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
        show_texture(sw.ren, gtex);
        present_output(sw.ren);
        SDL_Delay(1000 / 18);
    }
    // FADE the death music out — do NOT hard-cut it (EXE FUN_2bd7_02e7 ends
    // with MDI_FadeStop; a hard stop chops MORT.MDI mid-loop, audibly wrong).
    audio.fade_out_music();
    SDL_DestroyTexture(gtex);
}

// The win ending's decoded assets: COOL3.PC1 as an RGBA background, COOL2.MAT's
// first sprite (the rising caveman), the palette.  Each failure logs its own
// message.
struct WinEndingScene {
    FrameBuffer bg;
    formats::Sprite sprite;
    std::vector<formats::Rgb> palette;
};

std::optional<WinEndingScene> load_win_ending_scene(
    const formats::CurArchive& eva) {
    if (!eva.contains("COOL3.PC1") || !eva.contains("COOL2.MAT")) {
        std::fprintf(stderr, "ending: assets missing (COOL3.PC1 / "
                             "COOL2.MAT not in FILESA.VGA)\n");
        return std::nullopt;
    }
    const formats::Pc1Image bg = formats::parse_pc1(eva.get("COOL3.PC1").data);
    if (bg.width != 320) {
        std::fprintf(stderr, "ending: COOL3.PC1 unexpected width\n");
        return std::nullopt;
    }
    const std::vector<formats::Sprite> mat_sprites =
        formats::load_mat_sprites(&eva.get("COOL2.MAT").data, "COOL2.MAT");
    if (mat_sprites.empty()) {
        std::fprintf(stderr, "ending: COOL2.MAT has no sprites\n");
        return std::nullopt;
    }
    WinEndingScene scene;
    scene.sprite = mat_sprites[0];
    scene.bg = pc1_frame(bg);
    scene.palette.assign(bg.palette.begin(), bg.palette.end());
    return scene;
}

namespace {

// Game_WinSequence's rise (EXE 0x0207-0x0279): the COOL2.MAT caveman from
// y 198 up to 73 at 2 per 18 Hz step over COOL3.PC1.  Smooth motion
// interpolates: vsync-paced when the driver allows it, else 3 discrete
// sub-frames.  OLDUVAI_ENDING_SHOT dumps the frame at rise step
// OLDUVAI_ENDING_SHOT_FRAME (0 = the first; 30 is mid-climb) and quits.
// False when aborted (ESC, window close, the shot).
bool rise_caveman(const WinEndingScene& scene, ScaledWindow& sw,
                  SDL_Texture* tex, int hd_scale, const std::string& hd_profile,
                  bool smooth, bool& quit_requested) {
    constexpr int kYStart = 198;
    constexpr int kYEnd = 73;
    constexpr int kDY = 2;
    constexpr Uint32 kFrameMs = 1000 / 18;
    const char* const shot = std::getenv("OLDUVAI_ENDING_SHOT");
    const int shot_step = env_int("OLDUVAI_ENDING_SHOT_FRAME", 0);
    int rise_step = 0;
    const auto render_at = [&](int y, Uint32 delay_ms) -> bool {
        FrameBuffer fb = scene.bg;
        blit_sprite(fb, scene.sprite, scene.palette, 64, y);
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (handle_fullscreen_toggle(ev, sw.win)) continue;
            if (ev.type == SDL_QUIT ||
                (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
                return false;
        }
        upload_native_frame(tex, fb, hd_scale, hd_profile);
        show_texture(sw.ren, tex);
        if (shot != nullptr && rise_step == shot_step) {
            capture_renderer_output(sw.ren, shot);
            quit_requested = true;
            return false;
        }
        present_output(sw.ren);
        if (delay_ms > 0) SDL_Delay(delay_ms);
        return true;
    };
    const bool vsync = smooth_try_enable_vsync(sw.ren, smooth);
    // The vsync fill is the shared one.  The discrete path is not: the
    // helper skips the delay after its last sub-frame, this sequence delays
    // after every one (the helper would run the rise ~50% fast).
    SmoothPacer pacer{vsync, 3, kFrameMs, 0};
    bool aborted = false;
    int prev_y = kYStart;
    for (int y = kYStart; y >= kYEnd && !aborted; y -= kDY, ++rise_step) {
        const int y0 = prev_y;
        if (smooth && vsync) {
            smooth_fill_tick(pacer, [&](float a, int) {
                if (!aborted)
                    aborted = !render_at(
                        y0 + static_cast<int>(std::lround((y - y0) * a)), 0);
            });
        } else if (smooth) {
            for (int sub = 1; sub <= 3 && !aborted; ++sub)
                aborted = !render_at(y0 + (y - y0) * sub / 3, kFrameMs / 3);
        } else {
            aborted = !render_at(y, kFrameMs);
        }
        prev_y = y;
    }
    return !aborted;
}

// Any key press then release (EXE 0x02a7 Keyboard_WaitPressRelease); ESC and
// a window close end it at once.
void wait_press_release(SDL_Window* win) {
    bool pressed = false;
    SDL_Event ev;
    while (SDL_WaitEvent(&ev)) {
        if (ev.type == SDL_QUIT) return;
        if (handle_fullscreen_toggle(ev, win)) continue;
        if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) return;
            pressed = true;
        }
        if (ev.type == SDL_KEYUP && pressed) return;
    }
}

}  // namespace

void show_win_ending(const std::filesystem::path& game_dir, SdlAudio& audio,
                     ScaledWindow& sw, int hd_scale,
                     const std::string& hd_profile, bool smooth_motion,
                     bool& quit_requested) {
    const formats::CurArchive eva(prepare::slurp_file(game_dir / "FILESA.VGA"));
    const formats::CurArchive efa(prepare::slurp_file(game_dir / "FILESA.CUR"));
    SDL_Texture* etex =
        create_stream_tex(sw.ren, 320 * hd_scale, 200 * hd_scale);
    if (audio.music_available() && efa.contains("FIN.MDI")) {
        audio.play_music(efa.get("FIN.MDI").data,
                         formats::mdi_track_id("fin.mdi"));
    }
    // Game_WinSequence (FUN_2bd7_0183).  Missing assets: a silent return.
    const std::optional<WinEndingScene> scene = load_win_ending_scene(eva);
    if (scene && rise_caveman(*scene, sw, etex, hd_scale, hd_profile,
                              smooth_motion, quit_requested))
        wait_press_release(sw.win);   // hold the final frame
    // FADE the last music of the game out, as the EXE does: 0x02d2 is
    // MDI_FadeStop + MDI_FreeSlot(0), the same ramp the game-over screen and
    // the tally use.  A hard stop chops FIN.MDI mid-phrase on the last frame
    // the player ever sees.  The ramp is synchronous and lands on silence, so
    // the title's intro music starts over it, not through it.
    audio.fade_out_music();
    SDL_DestroyTexture(etex);
}

}  // namespace olduvai::presentation
