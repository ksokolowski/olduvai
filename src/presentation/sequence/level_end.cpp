// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/sequence/level_end.hpp"

#include <cstdio>
#include <cstdlib>

#include "presentation/audio/audio.hpp"
#include "presentation/audio/game_music.hpp"          // play_tally_music
#include "presentation/image_out.hpp"                 // save_rgba_image
#include "presentation/render/widescreen_presenter.hpp"
#include "presentation/window_util.hpp"               // handle_fullscreen_toggle

namespace olduvai::presentation {

namespace {

// OLDUVAI_DUMP_LEVEL_FADE=<dir>: each level-complete fade frame as a
// pre-upscale PNG (stb encoder: identical bytes on every platform, unlike
// SDL_SaveBMP).
void dump_level_fade(const std::vector<std::uint8_t>& px, int w, int h) {
    const char* dir = std::getenv("OLDUVAI_DUMP_LEVEL_FADE");
    if (dir == nullptr) return;
    static int level_fade_seq = 0;
    char p[512];
    std::snprintf(p, sizeof p, "%s/levelfade_%04d.png", dir, level_fade_seq++);
    save_rgba_image(px.data(), w, h, p);
}

void fade_wide_to_black(Loaded& g, WidescreenPresenter& wsp,
                        SDL_Window* win, int prev_px, int prev_py,
                        Uint32 frame_ms, bool& running) {
    // Keep the window full-width so the side bars do not pop in on frame 0.
    // Compose the last frame with the player back at its pre-pseudo-exit
    // position (transitions.cpp wrapped it to the left edge), wrap it wide, and
    // fade the wide buffer with with_hud=false (the HUD darkens with the scene,
    // as in classic).
    FrameBuffer last_center{};
    {
        const int sx = g.state.player.x, sy = g.state.player.y;
        g.state.player.x = prev_px;
        g.state.player.y = prev_py;
        RenderTarget rt{last_center.px.data(), 320, 200, 1, nullptr,
                        nullptr};
        rt.advance_state = false;
        compose_frame(rt, g.state, g.render, /*draw_player=*/true);
        g.state.player.x = sx;
        g.state.player.y = sy;
    }
    std::vector<std::uint8_t> last_wide;
    wsp.wrap_wide_static(last_center, last_wide);   // no edge-player mirror
    // L5 glider fly-away: the glider exits right past x=320, which the 320
    // centre clips.  Redraw the entities into the wide buffer with right
    // overflow allowed so it flies into the margin during the fade.
    if (g.state.current_level == 5 && g.state.glider_active) {
        const int sx = g.state.player.x, sy = g.state.player.y;
        g.state.player.x = prev_px;
        g.state.player.y = prev_py;
        RenderTarget wrt{last_wide.data(), wsp.native_w(), 200, 1,
                         nullptr, nullptr};
        wrt.origin_x = wsp.margin();
        wrt.advance_state = false;
        wrt.clip_x_lo = wsp.margin();   // protect the left margin
        presentation::draw_entities(wrt, g.state, g.render,
                                    /*draw_player=*/true);
        g.state.player.x = sx;
        g.state.player.y = sy;
    }
    FrameBuffer src{wsp.native_w(), 200};
    src.px = last_wide;
    FrameBuffer wf{wsp.native_w(), 200};
    bool quit = false;
    for (int f2 = 0; f2 <= kFadeFrames && !quit; ++f2) {
        apply_fade(wf, src, static_cast<double>(f2) / kFadeFrames);
        dump_level_fade(wf.px, wsp.native_w(), 200);
        wsp.present_transition(wf.px, /*with_hud=*/false);
        SDL_Event e2;
        while (SDL_PollEvent(&e2)) {
            if (handle_fullscreen_toggle(e2, win)) continue;
            if (e2.type == SDL_QUIT) { running = false; quit = true; }
        }
        SDL_Delay(frame_ms);
    }
}

}  // namespace

bool play_level_tally(const LevelTally& t) {
    play_tally_music(t.audio, t.game_dir);
    return t.screen.text_screen(
        t.deps, t.hd_ok, "OLDUVAI_DUMP_TALLY", "tally",
        [&t](const TextScreenHd& hd) {
            return show_score_tally(t.lives, t.score, t.display_level, 500,
                                    {t.charset, t.palette, t.present, hd},
                                    TallyAudio{t.audio, t.enhanced});
        });
}

bool play_platform_ending(const PlatformEnding& c, int end_px, int end_py) {
    bool fade_ok = true;
    if (c.wsp.active()) {
        fade_wide_to_black(c.g, c.wsp, c.win, end_px, end_py, c.frame_ms,
                           fade_ok);
    } else {
        fade_ok = fade_to_black(c.fb, c.present, [](const FrameBuffer& f) {
            dump_level_fade(f.px, f.w, f.h);
        });
    }
    if (!fade_ok || std::getenv("OLDUVAI_DUMP_LEVEL_FADE") != nullptr)
        return false;
    return play_level_tally({c.screen, c.text_screen_deps, c.use_hd_text,
                             c.g.state.player.lives, c.g.state.score,
                             c.display_level, c.g.charset, c.g.render.palette,
                             c.present, c.audio, c.game_dir, c.enhanced});
}

}  // namespace olduvai::presentation
