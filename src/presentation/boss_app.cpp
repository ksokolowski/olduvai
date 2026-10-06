// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss_app.hpp"

#include "presentation/boss/boss_diag.hpp"
#include "presentation/boss/boss_ending.hpp"
#include "presentation/boss/boss_fight.hpp"
#include "presentation/boss/boss_pause.hpp"
#include "presentation/boss/boss_view.hpp"


#include <SDL.h>

#include "presentation/image_out.hpp"

#include <cstdlib>     // std::getenv (OLDUVAI_WS_FORCE_MARGIN widescreen override)
#include <algorithm>   // std::copy_n (libstdc++ needs the full header; libc++
                       // pulls it in transitively, hence macOS-only build pass)
#include <cstring>
#include <fstream>

#include "presentation/audio/audio.hpp"
#include "presentation/audio/game_music.hpp"
#include "presentation/diag/bug_capture.hpp"
#include "presentation/diag/menu_script.hpp"
#include "presentation/render/rising_balloons.hpp"
#include "presentation/diag/report_form.hpp"
#include "presentation/diag/frame_stats.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/input/replay.hpp"
#include "presentation/sequence/screens.hpp"
#include "presentation/render/smooth_present.hpp"
#include "presentation/render/boss_arena.hpp"
#include "presentation/render/boss_hud.hpp"
#include "presentation/window_util.hpp"
#include "presentation/render/boss_render.hpp"
#include "presentation/render/level_surface.hpp"

namespace olduvai::presentation {

namespace {

using formats::Rgb;
using formats::Sprite;
using namespace olduvai::systems;


}  // namespace

BossRunResult run_boss_level(GameOptions& opts, int internal_level,
                             CarriedState& carry, Pipeline& pipe) {
    BossRunResult res;
    // The fight's OLDUVAI_* test hooks (boss/boss_diag.hpp).
    const BossHooks hooks = BossHooks::from_env();
    RunCapture capture;
    capture.open(opts.replay, opts.trace, opts.record_inputs);
    const InputReplay& replay = capture.replay;
    TraceWriter& trace = capture.trace;
    InputRecorder& input_rec = capture.input_rec;
    BossAssets assets;
    if (!load_boss_assets(opts.game_dir, internal_level, assets)) {
        std::fprintf(stderr, "boss: could not load arena assets\n");
        return res;
    }
    // Name the level the capture shows (no display/internal swap for bosses).
    std::fprintf(stderr, "game: level %d (internal %d)\n", internal_level,
                 internal_level);

    // The fight's display (boss/boss_view.hpp): the surface starts at the
    // aspect's pillarboxed logical size, so the loading card is not
    // stretched; the wide fight switches to the wide canvas below.  It uses
    // the level's scale, so the arena matches the level before.
    BossDisplay display(pipe, opts, [](const GameOptions& o) {
        return aspect_logical(
            hd_scale_for(o.enhanced, o.hd_profile, o.render_scale), o.aspect);
    });

    // OLDUVAI_FRAME_STATS: same counters and report as run_platform_level.
    FrameStats fstats;
    fstats.begin_run();

    // Pacing for the loading card, tally and victory fades; the main loop uses
    // DosTicker.
    const Uint32 frame_ms = 1000 / 18;

    BossFight fight{internal_level, init_boss_player(carry.lives, carry.score),
                    {}, {}, {}, {}};
    // L4 re-inits the player after the shared setup: FUN_24cc_02f2 writes
    // player_x := 0xD2 = 210 (Boss_L2_Init's 0x3c = 60 stays for L2 and L6).
    if (internal_level == 4) fight.player.x = 210;

    // No reseed at boss entry: the EXE never re-touches DS:0x87ac after static
    // init, so the LCG carries over from the previous level.

    // Smooth motion: vsync render interpolation (smooth_present.hpp), discrete
    // sub-frames without vsync.  A display rebuild recomputes it: a Style
    // change turns it over.
    bool smooth = boss_smooth_motion(opts, hooks.force_smooth);

    // Enhanced: the fly-in balloons float away when the fly-in ends (as on the
    // L1 landing).  The bunch sprite is in L1SPR.MAT, already loaded for the
    // menu cursor.  `boss_fx_alpha` lerps the rise on smooth sub-frames.
    RisingBalloons fly_in_balloons;
    float boss_fx_alpha = 1.0f;
    const auto draw_fly_in_balloons = [&](RenderTarget& rt) {
        fly_in_balloons.draw(rt, assets.bone_atlas, assets.bone_palette,
                             boss_fx_alpha);
    };

    // L2's victory renderer takes the flash index, so its arm captures this.
    int l2_last_flash = 0;   // last L2 victory flash frame — fade source parity

    // The fight's per-boss dispatch, the fly-away over every draw path.
    // Rebuilt with the display: `smooth` reaches L6's slam pacing.
    BossOps boss_ops =
        draw_after(make_boss_ops(fight, assets, l2_last_flash, smooth),
                   draw_fly_in_balloons);

    // The view draws through boss_ops as it is now, not a copy of it.
    display.open_view(
        {opts, assets, fight, fstats,
         [&boss_ops](RenderTarget& t) { boss_ops.render_sprites(t); }, smooth,
         frame_ms});

    // Non-gameplay screens (loading card, post-win fade, classic tally): ESC
    // is inert on all of them (no menu; a won fight must not become a game
    // over); only window close stops.
    const bool early_quit = !display.view().screen().text_screen(
        display.view().text_deps(), display.surface().use_hd_text(),
        "OLDUVAI_DUMP_LOADING", "loading", [&](const TextScreenHd& sh) {
            return show_loading_screen(
                nullptr, internal_level,
                {assets.charset, assets.palette, display.view().present(), sh});
        });
    if (early_quit) res.quit = true;

    const auto start_music = [&] {
        play_level_music(pipe.audio.get(), opts.game_dir, internal_level);
    };
    if (!early_quit) start_music();

    bool running = !early_quit;
    int frame = 0;

    BossPause pause(opts, pipe.audio.get(), pipe.sw.win, res, running);

    // F5: the platform level's report form (shared "bug_report" screen), bound
    // at the freeze below.  OLDUVAI_MENU_SCRIPT drives it
    // (tests/boss_report.sh).
    ReportFormService report_form(pause.model());
    MenuScript menu_script;
    menu_script.load_from_env();

    TickPacer pacer;                     // the end of each tick
    bool smooth_vsync_ran = false;       // set per-frame; skips the outer delay

    // A display or audio Apply from the pause, in place: the fight stays and
    // the pause stays open over the new display.  The shown arena is rebuilt
    // from the loaded one and the fight's drain, `smooth` from the adopted
    // Style (before the view: its pacer reads it), then the boss ops; the
    // music plays on unless the audio was replaced.
    const auto reinit_in_place = [&] {
        const BossReinit target = pause.reinit_target();
        const Rebuilt r = display.rebuild(
            target, &target.aspect, [&](bool vector_hud) {
                rebuild_arena_bg(assets, fight, vector_hud);
                smooth = boss_smooth_motion(opts, hooks.force_smooth);
                return true;
            });
        if (r == Rebuilt::kFailed) return false;
        boss_ops =
            draw_after(make_boss_ops(fight, assets, l2_last_flash, smooth),
                       draw_fly_in_balloons);
        pause.pipeline_changed(pipe.audio.get(), pipe.sw.win);
        display.view().use_wide_canvas();
        pacer.renderer_changed();
        if (r == Rebuilt::kAudioToo) start_music();
        return true;
    };

    // Loading is done: the wide logical canvas.
    display.view().use_wide_canvas();

    // The menus' bone cursor (L1SPR.MAT sprite 33).
    const formats::Sprite* const bone =
        assets.bone_atlas.size() > 33 ? &assets.bone_atlas[33] : nullptr;
    const std::vector<Rgb>* const bone_palette =
        bone != nullptr ? &assets.bone_palette : nullptr;

    while (running) {
        // The display's parts, bound per frame.
        const LevelSurface& surface = display.surface();
        BossView& view = display.view();
        BossArenaPresenter& arena = view.arena();
        BossWidescreen& wsb = view.ws();
        SmoothPos& smooth_pos = view.smooth_pos();
        fstats.begin_tick();
        cursor_autohide_frame();   // keyboard game: park the OS arrow
        pause.begin_frame();
        smooth_vsync_ran = false;   // set true only when the vsync fill paced
        // OLDUVAI_MENU_SCRIPT: one token before the poll, as on the platform.
        if (menu_script.active() &&
            drive_menu_script(menu_script, &report_form)) {
            res.quit = true;
            break;
        }
        menu_script.shot_path.clear();   // `shot` is a platform-only token
        poll_boss_events(pipe.sw.win, report_form, pause, replay.active(), res,
                         running);

        // OLDUVAI_BOSS_PAUSE_SHOT=<path>: open Pause after the fly-in and dump
        // one frame.
        if (hooks.pause_shot != nullptr && frame == 60 && !pause.is_open())
            open_pause_shot(pause, fight, smooth_pos, hooks);

        pause.track_options_exit();

        // F5 form over the frozen fight, presented like the pause menu's native
        // path.
        if (running && report_form.open()) {
            wsb.rebuild_if_resized();
            const auto show_native = [&](FrameBuffer& f) {
                arena.show_native(f);
            };
            const auto write_report = [&](const FrameBuffer& shot_fb,
                                          const BugAnnotations& ann) {
                write_boss_report(fight, boss_ops, frame, assets, view,
                                  shot_fb, ann);
            };
            if (report_form.service_freeze(
                    {[&](FrameBuffer& out) {
                         RenderTarget prt{out.px.data(), out.w, out.h, 1,
                                          nullptr, nullptr};
                         boss_ops.render_frame(prt);
                     },
                     show_native, write_report, assets.charset, bone,
                     bone_palette, frame_ms}))
                continue;
        }

        // Pause: the frozen fight + menu; logic is skipped.
        if (pause.is_open() && running) {
            // The parts bound above are the old view's: a rebuild starts the
            // next frame.  A failed one ends the program (the pipeline is
            // gone).
            if (pause.wants_reinit()) {
                if (!reinit_in_place()) {
                    res.quit = true;
                    res.quit_program = true;
                    running = false;
                }
                continue;
            }
            arena.show_pause(boss_ops.render_frame, boss_ops.render_sprites,
                             pause.menu(), pause.confirm(), assets.charset,
                             bone, bone_palette);

            if (hooks.pause_shot != nullptr) {   // headless: dump + exit
                // Read back before the buffer swap (black on Metal after it).
                capture_renderer_output(pipe.sw.ren, hooks.pause_shot);
                res.quit = true;
                res.quit_program = true;   // not game over -> title
                running = false;
                continue;
            }
            present_output(pipe.sw.ren);
            SDL_Delay(16);
            continue;
        }

        maybe_auto_fullscreen(pipe.sw.win, frame);

        const BossInputs in = read_boss_inputs(replay, frame);
        if (replay.active() && frame > replay.last_frame() + 18) {
            res.quit = true;
            running = false;
        }
        record_boss_inputs(input_rec, frame, in);

        if (smooth) save_prev_positions(fight);
        const int l4_health_before = fight.l4.health;
        const int l6_health_before = fight.l6.health;
        boss_ops.update_frame(in);

        // After the update, so the release lands on the tick the fly-in ends
        // (halo_flag counter).
        fly_in_balloons.step(opts.enhanced, fight.player.halo_flag > 0,
                             fight.player.death_counter == 0, fight.player.x,
                             fight.player.y - 34, 0);

        // Skip the fb compose when the wide present is active: it builds its
        // own buffer (saves 5.7-6.1 ms per present).  fb is still needed by the
        // resize fallback, F5 and --play-shot.  rebuild_if_resized() first, so
        // a just-taken Alt+Enter cannot leave the skip on while present falls
        // back to present_frame.
        wsb.rebuild_if_resized();
        const bool skip_compose = wsb.active && wsb.wtex() != nullptr &&
                                  opts.screenshot.empty();
        auto render_fight = [&]() {
            if (skip_compose) return;
            FrameStats::Timer ct(&fstats, &FrameStats::compose_ms);
            auto rt = view.target(view.fb());
            boss_smooth_pos(rt, smooth_pos.use_float, smooth_pos.fx,
                            smooth_pos.fy);
            boss_ops.render_fight_frame(rt);
        };

        // Smooth motion: sub-frames lerp the player, L2 projectile x and L4
        // dino x/y, each behind the 16 px teleport guard (L6: the player only;
        // the giant is anchored).  Animation timers tick once per logic frame,
        // after this block.
        if (smooth) {
            smooth_vsync_ran = smooth_fill_tick(view.pacer(), [&](float alpha,
                                                                   int) {
                boss_fx_alpha = alpha;   // this sub-frame's balloon rise
                const FightPositions logic =
                    interpolate_fight(fight, alpha, smooth_pos);
                render_fight();
                arena.present_any();
                restore_fight(fight, logic);
            });
            smooth_pos.use_float = false;   // the paths below draw integer
            boss_fx_alpha = 1.0f;
        } else {
            render_fight();
            arena.present_any();
        }

        // Post-render ticks + HUD pip erasure, once per logic tick after the
        // render (draw-then-increment; jaw/arm timers must not fire per
        // sub-frame).
        bool won = boss_post_render(fight, assets, l4_health_before,
                                    l6_health_before, !surface.use_hd_text());
        if (SdlAudio* const audio = pipe.audio.get()) {
            if (boss_ops.take_sfx_hit()) audio->play_sfx("SFX_HIT");
            if (fight.player.jump_apex_sfx_pending)
                audio->play_sfx("SFX_JUMP_APEX");
        }
        fight.player.jump_apex_sfx_pending = false;

        if (trace.active()) {
            trace.write_boss(frame, fight.player, boss_ops.health());
        }
        ++frame;
        if (!opts.screenshot.empty() && frame == opts.screenshot_frame) {
            arena.capture_shot(opts.screenshot, hooks.real_shot);
            running = false;
        }
        if (opts.frames > 0 && frame >= opts.frames) running = false;

        if (debug_force_win(fight, frame, hooks)) won = true;
        if (won) {
            res.survived = true;
            running = false;
        }
        if (fight.player.lives < 0) running = false;

        // Before the pacing wait, so `worst_work` excludes sleep.
        fstats.end_tick();

        // Counters omitted: the boss has no --vga-scan diagnostic to report.
        pacer.end_tick(pipe.sw.ren, surface.tex(), smooth_vsync_ran,
                       opts.vga_scan, surface.hd());
    }
    // Reported at the end of the fight; the victory reports on its own.
    fstats.report(internal_level);

    // Boss music plays through the victory until the tally's BONUS.MDI replaces
    // it (FUN_270a_01b4).  Stop it only when no victory follows.
    const bool victory_coming =
        res.survived && !res.quit && opts.frames <= 0 &&
        opts.screenshot.empty();
    if (pipe.audio && !victory_coming) pipe.audio->stop_music();
    carry.lives = fight.player.lives;
    carry.score = fight.player.score;

    BossView& view = display.view();
    const BossEnding ending{display.surface(), view.hd_cache(), view.arena(),
                            view.screen(),     view.text_deps(), view.present(),
                            assets,            fight.player,     view.fb(),
                            opts.screenshot,   frame_ms,         res,
                            pipe.audio.get(),  opts.game_dir,    opts.enhanced,
                            &fstats};
    play_boss_ending(ending, fight, boss_ops.render_victory_sprites,
                     l2_last_flash, smooth, view.pacer(), view.smooth_pos(),
                     opts.frames <= 0 &&
                         (opts.screenshot.empty() || hooks.real_shot));
    return res;
}

}  // namespace olduvai::presentation
