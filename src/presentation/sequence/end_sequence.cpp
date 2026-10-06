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
#include <string>
#include <utility>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "enhance/upscale.hpp"
#include "formats/cur.hpp"
#include "formats/mat.hpp"
#include "formats/mdi.hpp"
#include "formats/pc1.hpp"
#include "presentation/input/actions.hpp"         // key_is
#include "enhance/hd_asset_cache.hpp"
#include "presentation/render/dirty_frame.hpp"    // DirtyFrame
#include "presentation/render/game_render.hpp"    // FrameBuffer, blit_sprite
#include "presentation/env_num.hpp"         // env_int — ENDING_SHOT_FRAME
#include "presentation/image_out.hpp"      // capture_renderer_output, present_output
#include "presentation/sequence/screens.hpp"   // fade_to_black, kFadeFrames
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
                 key_is(ev.key.keysym.sym, Action::kBack))) {
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

// The ending's picture on `tex`.  Scale 1: COOL3 and the caveman composed
// native and uploaded whole.  HD: COOL3 upscaled once, the caveman's cached
// HD sprite over it through DirtyFrame, and only the rects he left or
// entered uploaded; upscaling the whole frame for every rise step and fade
// frame crawled at 4x.
class EndingCanvas {
  public:
    EndingCanvas(const WinEndingScene& scene, SDL_Texture* tex, int scale,
                 std::string profile)
        : scene_(scene), tex_(tex), s_(scale), profile_(std::move(profile)) {
        if (s_ > 1)
            bg_hd_ = enhance::upscale_rgba(scene_.bg.px, 320, 200, s_, profile_);
    }

    SDL_Texture* texture() const { return tex_; }

    // The caveman at native y, uploaded.
    void show(int y) {
        if (s_ == 1) {
            native_ = scene_.bg;
            blit_sprite(native_, scene_.sprite, scene_.palette, 64, y);
            SDL_UpdateTexture(tex_, nullptr, native_.px.data(), 320 * 4);
            return;
        }
        const int w = 320 * s_;
        const DirtyFrame::Background bg{bg_hd_, 1, w, 200 * s_};
        const std::vector<DirtyRect>* region = dirty_.compose(
            bg, [this](std::uint8_t* px) { return target(px); },
            [this, y](RenderTarget& rt) {
                blit_sprite(rt, scene_.sprite, scene_.palette, 64, y);
            },
            {}, /*allow=*/true, nullptr);
        const std::vector<std::uint8_t>& f = dirty_.frame();
        if (region == nullptr) {
            SDL_UpdateTexture(tex_, nullptr, f.data(), w * 4);
            return;
        }
        for (const DirtyRect& r : *region) {
            if (r.empty()) continue;
            const SDL_Rect sr{r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0};
            SDL_UpdateTexture(
                tex_, &sr,
                f.data() + (static_cast<std::size_t>(r.y0) * w + r.x0) * 4,
                w * 4);
        }
    }

    // The last shown frame faded by t (0 unchanged, 1 black), uploaded.
    void show_faded(double t) {
        fade_rgba(work_, s_ == 1 ? native_.px : dirty_.frame(), t);
        SDL_UpdateTexture(tex_, nullptr, work_.data(), 320 * s_ * 4);
    }

  private:
    RenderTarget target(std::uint8_t* px) {
        return RenderTarget{px, 320 * s_, 200 * s_, s_, &cache_, &profile_};
    }

    const WinEndingScene& scene_;
    SDL_Texture* tex_;
    int s_;
    std::string profile_;
    enhance::HdAssetCache cache_;
    std::vector<std::uint8_t> bg_hd_, work_;
    DirtyFrame dirty_;
    FrameBuffer native_;
};

// How the climb ended: it reached the top, a press cut it short, or the
// program is going (window close, the shot).
enum class Rise { kTop, kSkipped, kQuit };

// Game_WinSequence's rise (EXE 0x0207-0x0279): the COOL2.MAT caveman from
// y 198 up to 73 at 2 per 18 Hz step over COOL3.PC1.  Smooth motion
// interpolates: vsync-paced when the driver allows it, else 3 discrete
// sub-frames.  OLDUVAI_ENDING_SHOT dumps the frame at rise step
// OLDUVAI_ENDING_SHOT_FRAME (0 = the first; 30 is mid-climb) and quits.
// OLDUVAI_ENDING_SKIP=<step> presses at that step (the skip's test hook);
// the shot then counts fade frames instead.
Rise rise_caveman(EndingCanvas& canvas, ScaledWindow& sw, bool smooth,
                  bool& quit_requested) {
    constexpr int kYStart = 198;
    constexpr int kYEnd = 73;
    constexpr int kDY = 2;
    constexpr Uint32 kFrameMs = 1000 / 18;
    const int skip_step = env_int("OLDUVAI_ENDING_SKIP", -1);
    const char* const shot =
        skip_step < 0 ? std::getenv("OLDUVAI_ENDING_SHOT") : nullptr;
    const int shot_step = env_int("OLDUVAI_ENDING_SHOT_FRAME", 0);
    int rise_step = 0;
    const auto render_at = [&](int y, Uint32 delay_ms) -> Rise {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (handle_fullscreen_toggle(ev, sw.win)) continue;
            if (ev.type == SDL_QUIT) return Rise::kQuit;
            if (ev.type == SDL_KEYDOWN &&
                key_is(ev.key.keysym.sym, Action::kBack))
                return Rise::kSkipped;
        }
        if (rise_step == skip_step) return Rise::kSkipped;
        canvas.show(y);
        show_texture(sw.ren, canvas.texture());
        if (shot != nullptr && rise_step == shot_step) {
            capture_renderer_output(sw.ren, shot);
            quit_requested = true;
            return Rise::kQuit;
        }
        present_output(sw.ren);
        if (delay_ms > 0) SDL_Delay(delay_ms);
        return Rise::kTop;
    };
    const bool vsync = smooth_try_enable_vsync(sw.ren, smooth);
    // The vsync fill is the shared one.  The discrete path is not: the
    // helper skips the delay after its last sub-frame, this sequence delays
    // after every one (the helper would run the rise ~50% fast).
    SmoothPacer pacer{vsync, 3, kFrameMs, 0};
    Rise end = Rise::kTop;
    int prev_y = kYStart;
    for (int y = kYStart; y >= kYEnd && end == Rise::kTop;
         y -= kDY, ++rise_step) {
        const int y0 = prev_y;
        if (smooth && vsync) {
            smooth_fill_tick(pacer, [&](float a, int) {
                if (end == Rise::kTop)
                    end = render_at(
                        y0 + static_cast<int>(std::lround((y - y0) * a)), 0);
            });
        } else if (smooth) {
            for (int sub = 1; sub <= 3 && end == Rise::kTop; ++sub)
                end = render_at(y0 + (y - y0) * sub / 3, kFrameMs / 3);
        } else {
            end = render_at(y, kFrameMs);
        }
        prev_y = y;
    }
    return end;
}

// Any key press then release (EXE 0x02a7 Keyboard_WaitPressRelease); Back
// ends it at once.  False when the window closed instead.
bool wait_press_release(SDL_Window* win) {
    bool pressed = false;
    SDL_Event ev;
    while (SDL_WaitEvent(&ev)) {
        if (ev.type == SDL_QUIT) return false;
        if (handle_fullscreen_toggle(ev, win)) continue;
        if (ev.type == SDL_KEYDOWN) {
            if (key_is(ev.key.keysym.sym, Action::kBack)) return true;
            pressed = true;
        }
        if (ev.type == SDL_KEYUP && pressed) return true;
    }
    return false;
}

// Enhanced: the frame a press left on screen fades to black while FIN.MDI
// ramps down with it, instead of the hard cut to the intro.  Three seconds,
// paced by the clock so a slow present does not stretch it.  A QoL
// divergence like the tally's music fade; whether the EXE darkens here is
// unrecorded.  OLDUVAI_ENDING_SHOT (with
// OLDUVAI_ENDING_SKIP) dumps fade frame OLDUVAI_ENDING_SHOT_FRAME and quits.
constexpr int kEndingFadeFrames = 54;   // 3 s at 18 frames/s

void fade_ending(EndingCanvas& canvas, SdlAudio& audio, ScaledWindow& sw,
                 bool& quit_requested) {
    const char* const shot = std::getenv("OLDUVAI_ENDING_SHOT");
    const int shot_frame = env_int("OLDUVAI_ENDING_SHOT_FRAME", 0);
    const Uint32 start = SDL_GetTicks();
    for (int frame = 0; frame <= kEndingFadeFrames; ++frame) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT) return;
        audio.set_music_fade(1.0f - static_cast<float>(frame) /
                                        kEndingFadeFrames);
        canvas.show_faded(static_cast<double>(frame) / kEndingFadeFrames);
        show_texture(sw.ren, canvas.texture());
        if (shot != nullptr && frame == shot_frame) {
            capture_renderer_output(sw.ren, shot);
            quit_requested = true;
            return;
        }
        present_output(sw.ren);
        const Uint32 due =
            start + static_cast<Uint32>(frame + 1) * 1000 / 18;
        const Uint32 now = SDL_GetTicks();
        if (due > now) SDL_Delay(due - now);
    }
}

}  // namespace

void show_win_ending(const std::filesystem::path& game_dir, SdlAudio& audio,
                     ScaledWindow& sw, int hd_scale,
                     const std::string& hd_profile, bool smooth_motion,
                     bool enhanced, bool& quit_requested) {
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
    if (scene) {
        EndingCanvas canvas(*scene, etex, hd_scale, hd_profile);
        Rise end = rise_caveman(canvas, sw, smooth_motion, quit_requested);
        if (end == Rise::kTop)   // hold the final frame for a press
            end = wait_press_release(sw.win) ? Rise::kSkipped : Rise::kQuit;
        if (enhanced && end == Rise::kSkipped)
            fade_ending(canvas, audio, sw, quit_requested);
    }
    // FADE the last music of the game out, as the EXE does: 0x02d2 is
    // MDI_FadeStop + MDI_FreeSlot(0), the same ramp the game-over screen and
    // the tally use.  A hard stop chops FIN.MDI mid-phrase on the last frame
    // the player ever sees.  The ramp is synchronous and lands on silence, so
    // the title's intro music starts over it, not through it.
    audio.fade_out_music();
    SDL_DestroyTexture(etex);
}

}  // namespace olduvai::presentation
