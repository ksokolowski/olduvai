// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss/boss_ending.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

#include <SDL.h>

#include "presentation/image_out.hpp"
#include "presentation/render/lerp_snapshot.hpp"
#include "presentation/sequence/level_end.hpp"   // play_level_tally
#include "presentation/window_util.hpp"

namespace olduvai::presentation {

namespace {

namespace {

// One victory frame as one FrameStats tick.  end() before the frame's own
// delay, so worst_work excludes the sleep as the fight's ticks do.
class VictoryTick {
public:
    explicit VictoryTick(FrameStats* fs) : fs_(fs) {
        if (fs_ != nullptr) fs_->begin_tick();
    }
    ~VictoryTick() { end(); }
    VictoryTick(const VictoryTick&) = delete;
    VictoryTick& operator=(const VictoryTick&) = delete;
    void end() {
        if (fs_ != nullptr) fs_->end_tick();
        fs_ = nullptr;
    }

private:
    FrameStats* fs_;
};

}  // namespace

// L2 victory: the 18-frame defeated-T-Rex flash.  `last_flash` keeps the last
// frame shown, for the fade source.
void play_l2_victory(const BossEnding& c, int& last_flash) {
    // 18-frame flash; each frame holds 7 ticks (lcall(15) through the
    // delay shim at 1847:168f, arg>>1): ~388 ms, ~7 s total.
    constexpr int kVictoryFrames = 18;
    constexpr Uint32 kFlashExtraMs = 388;   // 1000 * 7 / 18 ≈ 388
    for (int vf = 0; vf < kVictoryFrames && !c.res.quit; ++vf) {
        VictoryTick tick(c.stats);
        last_flash = vf;   // remember for the post-victory fade parity
        // The native frame: the fade source, and the resize fallback.
        FrameBuffer vnat;
        if (c.arena.wide_on()) {
            RenderTarget rt{vnat.px.data(), 320, 200, 1, nullptr,
                            nullptr};
            render_l2_victory_frame(rt, c.assets, c.player, vf);
        } else {
            auto rt = c.target(c.fb);
            render_l2_victory_frame(rt, c.assets, c.player, vf);
        }

        // F4: the lives digit is not drawn (EXE draw_lives=false); the
        // labels are. Won: only a window close stops the flash.
        if (!poll_screen_events(c.surface.win())) c.res.quit = true;
        if (!c.res.quit) {
            if (std::getenv("OLDUVAI_REAL_SHOT") != nullptr && c.arena.wide_on() &&
                vf == 8 && !c.shot.empty()) {
                // Debug: capture a mid-flash victory frame (real
                // output), through the path the flash presents on.
                c.arena.present_wide_hd(
                    [&](RenderTarget& wrt) {
                        render_l2_victory_sprites(wrt, c.assets, c.player,
                                                  vf);
                    },
                    /*draw_lives=*/false, /*do_present=*/false);
                capture_renderer_output(c.surface.ren(), c.shot);
                c.res.quit = true;   // captured; stop the sequence
            } else if (c.arena.wide_ready()) {
                // The fight's clean HD arena, then the victory sprites at
                // origin_x = wsb.M.  Mirroring the baked frame would
                // reflect the T-Rex tail into the margin.
                c.arena.keep_fade_source(vnat);
                c.arena.present_wide_hd(
                    [&](RenderTarget& wrt) {
                        render_l2_victory_sprites(wrt, c.assets, c.player,
                                                  vf);
                    },
                    /*draw_lives=*/false);
                tick.end();
                SDL_Delay(kFlashExtraMs);
            } else if (c.arena.wide_on()) {
                c.arena.present_wide_native(vnat, /*draw_lives=*/false);  // resize fallback
                SDL_Delay(kFlashExtraMs);
            } else {
                c.arena.present_frame(/*draw_lives=*/false);
                SDL_Delay(kFlashExtraMs);
            }
        }
    }
}

// L6 victory: the player drops, then a 30-count defeat cycle.
void play_l6_victory(const BossEnding& c, L6BossState& l6, bool smooth,
                     SmoothPacer& pacer, SmoothPos& sp) {
    // Static backdrop, built once.
    FrameBuffer victory_bg_fb;   // always 320x200 for the blit helpers
    // Fill from current bg.
    std::copy(c.assets.bg.begin(), c.assets.bg.end(),
              victory_bg_fb.px.begin());
    // Beaten pose: H3.MAT sprite 0 at (80,7).  Each H*.MAT holds one
    // 240x190 pose, so the source is h3[0], not h1[2].
    {
        RenderTarget vt{victory_bg_fb.px.data(), 320, 200, 1,
                        nullptr, nullptr};
        blit_at(vt, c.assets.h3, 0, c.assets.palette, 80, 7);

        // L6SPR[49] at (213,7) — beaten face sprite.
        if (static_cast<int>(c.assets.spr.size()) > 49)
            blit_sprite(vt, c.assets.spr[49], c.assets.palette, 213, 7);
    }

    // Wide L6 victory: clean arena mirror + sprites once at origin_x =
    // wsb.M, so the giant and player are not reflected.  Falls back to
    // present_wide_native if a resize drops the wide texture.
    auto present_l6_victory_wide = [&](bool do_present = true) {
        c.arena.rebuild_if_resized();
        FrameBuffer vnat;
        {
            RenderTarget rt{vnat.px.data(), 320, 200, 1, nullptr,
                            nullptr};
            render_l6_victory_frame(rt, victory_bg_fb.px, c.assets, c.player,
                                    l6);
        }
        if (!c.arena.wide_ready()) {
            c.arena.present_wide_native(vnat);   // resize fallback
            return;
        }
        c.arena.keep_fade_source(vnat);      // post-victory fade source
        // The fight's clean HD arena, sprites at HD.  do_present=false
        // renders without flipping, for the capture hook.
        c.arena.present_wide_hd(
            [&](RenderTarget& wrt) {
                // Sub-pixel drop on the smooth path; inert when
                // use_float is false.
                boss_smooth_pos(wrt, sp.use_float, sp.fx, sp.fy);
                render_l6_victory_sprites(wrt, c.assets, c.player, l6);
            },
            /*draw_lives=*/true, do_present);
    };

    // Reset victory state in case fight loop already set win_flag=100.
    l6.win_flag = 1;
    l6.win_counter = 0;
    l6.cycle_idx = 0;
    l6.cycle_tick = 0;

    // Victory-frame counter for the capture hook (as L2/L4 have).
    int l6_vf = 0;
    while (l6.win_flag != 100 && !c.res.quit) {
        VictoryTick tick(c.stats);
        const Uint32 vt0 = SDL_GetTicks();
        // Won: only a window close stops the ride-off.
        if (!poll_screen_events(c.surface.win())) c.res.quit = true;

        // cycle_idx / win_flag advance once per logic frame; only the
        // drop is lerped.
        const int prev_y = c.player.y;
        update_l6_victory_frame(c.player, l6);
        const int cur_y  = c.player.y;
        const bool is_dropping = (cur_y != prev_y);

        // OLDUVAI_REAL_SHOT: capture a mid-drop frame.  Before the
        // smooth branch, so it is rendered at integer positions and
        // stable as a golden.
        if (std::getenv("OLDUVAI_REAL_SHOT") != nullptr && c.arena.wide_on() &&
            l6_vf == 8 && !c.shot.empty()) {
            present_l6_victory_wide(/*do_present=*/false);
            capture_renderer_output(c.surface.ren(), c.shot);
            c.res.quit = true;   // captured; stop the sequence
            break;
        }
        ++l6_vf;

        if (smooth && is_dropping) {
            // Lerp player y across the sub-frames.  The drop (+4) never
            // trips the guard today; it stays in case kDropDY changes.
            smooth_fill_tick(pacer, [&](float alpha, int) {
                c.player.y = snap_lerp_i(prev_y, cur_y, alpha);
                // Float shadow: the integer y moves 16 output pixels
                // per tick at hd_scale 4, which three sub-frames cannot
                // smooth.
                sp = {true, static_cast<float>(c.player.x),   // x is fixed
                      snap_lerp_f(prev_y, cur_y, alpha)};
                if (c.arena.wide_on()) {
                    present_l6_victory_wide();   // clean bg + sprites@wsb.M
                } else {
                    auto rt = c.target(c.fb);
                    boss_smooth_pos(rt, sp.use_float, sp.fx, sp.fy);
                    render_l6_victory_frame(rt, victory_bg_fb.px,
                                            c.assets, c.player, l6);
                    c.arena.present_frame();
                }
                c.player.y = cur_y;   // restore before next sub-frame
            });
            // No stale float state for the landed frames or the fade.
            sp.use_float = false;
        } else {
            if (c.arena.wide_on()) {
                present_l6_victory_wide();   // clean bg + sprites@wsb.M
            } else {
                auto rt = c.target(c.fb);
                render_l6_victory_frame(rt, victory_bg_fb.px, c.assets,
                                        c.player, l6);
                c.arena.present_frame();
            }
            tick.end();
            const Uint32 vspent = SDL_GetTicks() - vt0;
            if (vspent < c.frame_ms) SDL_Delay(c.frame_ms - vspent);
        }
    }
}

// Fade to black, then the score tally (Level_EndScreen(N, 500)).
void fade_to_tally(const BossEnding& c, int internal_level,
                   const std::function<void(RenderTarget&)>& victory_sprites) {
    if (c.arena.wide_on()) {
        // Widescreen: fade the last victory frame (not the stale fight fb)
        // wrapped wide, no HUD.
        c.arena.use_wide_logical();

        // L2, L4 and L6 fade through the overflow compose: each has a
        // victory sprite at the screen edge that mirroring the baked frame
        // would duplicate.  Build the wide HD frame once, as the victory
        // drew it, then darken it per frame.
        if (c.arena.wide_ready() &&
            (internal_level == 4 || internal_level == 2 ||
             internal_level == 6)) {
            const std::vector<std::uint8_t> wbase =
                c.arena.build_wide_hd(victory_sprites);
            for (int f2 = 0; f2 <= kFadeFrames && !c.res.quit; ++f2) {
                const double k =
                    1.0 - static_cast<double>(f2) / kFadeFrames;
                std::vector<std::uint8_t> faded = wbase;
                for (std::size_t i = 0; i < faded.size(); i += 4) {
                    faded[i]     = static_cast<std::uint8_t>(faded[i] * k);
                    faded[i + 1] = static_cast<std::uint8_t>(faded[i + 1] * k);
                    faded[i + 2] = static_cast<std::uint8_t>(faded[i + 2] * k);
                }
                c.arena.show_wide_up(faded, /*draw_lives=*/false,
                                     /*do_present=*/true,
                                     /*draw_hud=*/false);
                if (!poll_screen_events(c.surface.win())) c.res.quit = true;
                SDL_Delay(c.frame_ms);
            }
        } else {
            for (int f2 = 0; f2 <= kFadeFrames && !c.res.quit; ++f2) {
                FrameBuffer faded;
                apply_fade(faded, c.arena.last_wide_native(),
                           static_cast<double>(f2) / kFadeFrames);
                c.arena.present_wide_native(faded, /*draw_lives=*/false,
                                    /*do_present=*/true, /*draw_hud=*/false);
                if (!poll_screen_events(c.surface.win())) c.res.quit = true;
                SDL_Delay(c.frame_ms);
            }
        }
        // The tally stays on the wide logical (see above).
    } else {
        // Classic: fade a native 320x200 copy of the arena frame.
        FrameBuffer fade_src;   // 320x200 for the lpresent path
        if (c.surface.hd()) {
            // Downscale the HD fb to 320x200 (nearest-neighbour).
            for (int y = 0; y < 200; ++y)
                for (int x = 0; x < 320; ++x) {
                    const int s = c.surface.hd_scale();
                    const std::size_t so =
                        (static_cast<std::size_t>(y * s) * c.fb.w + x * s) * 4;
                    const std::size_t do_ =
                        (static_cast<std::size_t>(y) * 320 + x) * 4;
                    fade_src.px[do_]     = c.fb.px[so];
                    fade_src.px[do_ + 1] = c.fb.px[so + 1];
                    fade_src.px[do_ + 2] = c.fb.px[so + 2];
                    fade_src.px[do_ + 3] = c.fb.px[so + 3];
                }
        } else {
            fade_src = c.fb;   // classic: fb is already 320x200
        }
        if (!fade_to_black(fade_src, c.lpresent)) c.res.quit = true;
    }
    if (!c.res.quit) {
        // Present the tally 1:1 with the window in every mode: rows centre
        // on the output, artwork on the logical canvas (see "Fade + tally"
        // above).  Without widescreen the canvas is the 640x400 pillarbox,
        // 127 px off centre.
        {
            int ow = 0, oh = 0;
            if (SDL_GetRendererOutputSize(c.surface.ren(), &ow, &oh) == 0 &&
                ow > 0 && oh > 0) {
                c.surface.lsz().set(ow, oh);
            }
        }
        // Boss levels are the same in display and internal numbering (only
        // 3/5 swap).
        const int display_level = internal_level;

        if (!play_level_tally({c.screen, c.text_screen_deps,
                               c.surface.use_hd_text(), c.player.lives,
                               c.player.score, display_level, c.assets.charset,
                               c.assets.palette, c.lpresent, c.audio,
                               c.game_dir, c.enhanced}))
            c.res.quit = true;
    }
}

}  // namespace

void play_boss_ending(const BossEnding& c, BossFight& f,
                      const std::function<void(RenderTarget&)>& victory_sprites,
                      int& l2_last_flash, bool smooth, SmoothPacer& pacer,
                      SmoothPos& sp, bool with_cinematic) {
    if (!c.res.survived || c.res.quit) return;
    if (with_cinematic) {
        // The victory is a stats phase of its own (the fight has reported).
        // L4's ride-off already ran inside the fight loop (it exits at
        // win_flag >= 100), so L4 reports no victory phase.
        if (c.stats != nullptr) c.stats->begin_phase();
        if (f.level == 2)
            play_l2_victory(c, l2_last_flash);
        else if (f.level == 6)
            play_l6_victory(c, f.l6, smooth, pacer, sp);
        if (c.stats != nullptr) c.stats->report(f.level, "victory");
    }
    if (!c.res.quit) fade_to_tally(c, f.level, victory_sprites);
}

}  // namespace olduvai::presentation
