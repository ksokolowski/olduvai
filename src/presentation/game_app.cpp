// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/input/frame_input.hpp"   // gather_frame_inputs
#include "presentation/game_app.hpp"

#include "presentation/input/gamepad.hpp"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "presentation/input/autofire.hpp"
#include "presentation/render/game_render.hpp"
#include "presentation/level/level_save.hpp"
#include "presentation/diag/level_diag.hpp"
#include "presentation/render/frame_presenter.hpp"
#include "presentation/render/lerp_snapshot.hpp"
#include "presentation/diag/reinit_test_hook.hpp"
#include "presentation/level/level_setup.hpp"
#include "presentation/level/level_state.hpp"
#include "presentation/sequence/l3_end_level.hpp"
#include "presentation/level/level_fx.hpp"
#include "presentation/level/platform_level.hpp"
#include "presentation/level/level_view.hpp"
#include "presentation/level/tick_render.hpp"
#include "presentation/sequence/level_end.hpp"
#include "presentation/sequence/screen_change.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/diag/menu_script.hpp"
#include "presentation/input/replay.hpp"
#include "presentation/audio/audio.hpp"
#include "presentation/boss_app.hpp"
#include "presentation/audio/game_music.hpp"
#include "presentation/menu/pause_service.hpp"
#include "presentation/diag/report_form.hpp"
#include "presentation/sequence/screens.hpp"
#include "presentation/render/widescreen_presenter.hpp"
#include "presentation/window_util.hpp"
#include "systems/frame_runner.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <array>
#include <functional>
#include <memory>
#include <map>
#include <optional>

#include "presentation/menu/cheat_picker.hpp"
#include "systems/collision_dispatch.hpp"
#include "systems/transitions.hpp"
#include "presentation/env_num.hpp"
#include "presentation/render/level_surface.hpp"

namespace olduvai::presentation {

namespace {

// A level's entry state after load_level: carried lives and score, --god,
// OLDUVAI_FORCE_FOOD, the enhanced teleport arrival, a save restore, and the
// vector HUD's label erase.
void prepare_level_state(Loaded& g, const GameOptions& opts, int internal,
                         const CarriedState& carry,
                         const std::optional<SaveState>& restore_in,
                         bool god_active, bool use_hd_text) {
    // L5 enhanced: one flat water body under the glider.
    setup_enhanced_glider_water(g, opts.enhanced, internal);
    g.state.player.lives = carry.lives;
    g.state.score = carry.score;
    g.state.god_mode = god_active;
    g.state.enhanced_active = opts.enhanced;   // render-only cosmetic gates
    if (god_active) {
        g.state.player.energy = 999;
        g.state.player.lives = 99;     // EXE cap
        g.state.food_count = systems::kFoodGate;  // full belly
    }
    // OLDUVAI_FORCE_FOOD=<n>: start with n food.  --god cannot under --replay,
    // and tests/food_gate_transition.sh needs the gate screen both ways.
    if (const int food = env_int("OLDUVAI_FORCE_FOOD", -1); food >= 0)
        g.state.food_count = food;

    // Enhanced level start: the spawn plays the teleport arrival (clouds, then
    // PLAYER_TURN) before the drop.  Fresh entries only: a restore moves the
    // player away from the spawn anchor.
    if (opts.enhanced && !restore_in) {
        g.state.teleport_in_ticks = 15;
        g.state.teleport_fx_x = g.state.player.x;
        g.state.teleport_fx_y = g.state.player.y;
    }

    // Full-state restore: saved header, RNG reseed, per-screen entity state,
    // exact screen re-bind, live entities.
    if (restore_in) apply_save(*restore_in, g);

    // Enhanced: vector text replaces the pre-baked GET READY / NOT ENOUGH FOOD
    // sprites (draw_entities checks this).  g.render persists across in-place
    // reloads.
    g.render.enhanced_vector_banners = use_hd_text;
    if (!use_hd_text) return;
    // Erase the baked HUD labels and gauge outline (rows 0-8) from the
    // background once; the vector HUD replaces them.  Source-level, so sprites
    // crossing the band (the death angel) still draw over it.
    auto& bgp = g.render.background.pixels;
    if (g.render.background.width == 320 && bgp.size() >= 9 * 320)
        std::fill(bgp.begin(), bgp.begin() + 9 * 320, bgp[0]);
    g.render.hud_strip.clear();   // the cave/secret strip carries them too
}

// The level's data from the game files with its entry state, or the
// snapshot's (`restore`): the level entry, and the display reinit.  The
// vector HUD extends the backdrop up through the HUD strip at bind time (L7
// adds a lavarock row at y=-54; PC1 levels mirror at compose time) — keyed on
// use_hd_text, not HD: without the vector HUD the baked labels in rows 0-8
// still draw, and the extension would cover them.
bool load_level_data(Loaded& g, const GameOptions& opts, int internal,
                     const CarriedState& carry,
                     const std::optional<SaveState>& restore, bool god_active,
                     bool use_hd_text) {
    g.render.extend_top_backdrop = use_hd_text;
    if (!load_level(opts.game_dir, g, internal, opts.start_screen)) {
        std::fprintf(stderr, "game: could not load level data from %s\n",
                     opts.game_dir.string().c_str());
        return false;
    }
    prepare_level_state(g, opts, internal, carry, restore, god_active,
                        use_hd_text);
    return true;
}

// The pause's verdict as the level's outcome (kFroze, kReinitDisplay and
// kNone never reach here).
LevelOutcome pause_outcome(PauseService::FreezeResult r,
                           const PauseService& pause, int& out_warp_display) {
    using R = PauseService::FreezeResult;
    switch (r) {
        case R::kRestartLevel:   return LevelOutcome::kRestartLevel;
        case R::kLoadCheckpoint: return LevelOutcome::kLoadCheckpoint;
        case R::kWarpLevel:
            out_warp_display = pause.want_warp();
            return LevelOutcome::kWarpLevel;
        case R::kAbortGameOver:  return LevelOutcome::kGameOver;
        case R::kQuitProgram:    return LevelOutcome::kQuitProgram;
        case R::kNone:
        case R::kFroze:
        case R::kReinitDisplay:  break;
    }
    return LevelOutcome::kQuit;
}

// This frame's pending sound events.
void play_pending_sfx(systems::SystemsState& st, SdlAudio& audio) {
    const auto play = [&audio](bool& pending, const char* sfx) {
        if (!pending) return;
        audio.play_sfx(sfx);
        pending = false;
    };
    play(st.sfx_hit_pending, "SFX_HIT");
    play(st.sfx_generic_pending, "SFX_GENERIC");
    play(st.sfx_spring_pending, "SFX_JUMP_APEX");
    play(st.jump_apex_sfx_pending, "SFX_JUMP_APEX");
}

// One frame's events.  The F5 form, the pause and the cheat picker each own
// input while open, in that order.  ESC / window close abort to the title
// through the game-over path (as the reference does); intentional divergence:
// the EXE quits straight to DOS (INT 9 latches DS:0x87eb -> FUN_210c_0c53 ->
// INT 21h/4Ch).
void poll_level_events(SDL_Window* win, ReportFormService& form,
                       PauseService& pause, CheatPicker& cheats,
                       systems::SystemsState& st, bool cheats_allowed,
                       bool& abort_to_title) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (handle_fullscreen_toggle(ev, win)) continue;
        if (ev.type == SDL_QUIT) abort_to_title = true;
        if (form.open()) {
            form.handle_event(ev);
            continue;
        }
        if (ev.type != SDL_KEYDOWN) continue;
        const auto sym = ev.key.keysym.sym;
        if (pause.open()) {
            pause.handle_keydown(sym);
            continue;
        }
        if (cheats.handle_key(sym, [&st](int bt) {
                systems::dispatch_bonus_activate(st, bt);
                std::printf("cheat: granted %s\n", CheatPicker::name(bt));
            }))
            continue;
        if (sym == SDLK_ESCAPE)
            pause.esc_pressed();   // opens Pause, or aborts to the title
        else if (sym == SDLK_F5)
            form.open_form();
        else if (sym == SDLK_F7 && cheats_allowed)
            cheats.open_picker();
    }
}

// Enhanced widescreen: birds despawn and respawn past the wide edge, not at
// the EXE's x < -50 / 355 (inside the margin).  Classic keeps the EXE bound,
// and so does a replay or trace session: the extension changes bird lifetime
// and respawn phase, so a recording would desync across modes.
int bird_margin(const WidescreenPresenter& wsp, bool recording) {
    return wsp.active() && !recording ? wsp.margin() : 0;
}

// --play-frames, or OLDUVAI_SMOOTH_FRAMES (the same bound without turning
// smooth motion off).
bool frame_limit_reached(const GameOptions& opts, int frame,
                         const LevelHooks& hooks) {
    return (opts.frames > 0 && frame >= opts.frames) ||
           frame >= hooks.smooth_frames;
}

}  // namespace

LevelOutcome run_platform_level(GameOptions& opts, int display_level,
                                int internal, CarriedState& carry,
                                Pipeline& pipe,
                                const std::optional<SaveState>& restore_in,
                                std::optional<SaveState>& out_load,
                                int& out_warp_display) {
    ScaledWindow& sw = pipe.sw;   // an adopt rebuilds it in place
    RunCapture capture;
    capture.open(opts.replay, opts.trace, opts.record_inputs);
    InputReplay& replay = capture.replay;
    TraceWriter& trace = capture.trace;
    InputRecorder& input_rec = capture.input_rec;

    // The display (level/level_view.hpp): its surface now, the view once the
    // level is bound; a display reinit rebuilds both in place.
    LevelDisplay display(pipe, opts,
                         [](const GameOptions&) { return LogicalDims{0, 0}; });

    // --god: 99 lives / 999 energy / no death.  Off under replay (determinism).
    // Mutable: Pause -> Cheats toggles it live.
    bool god_active = opts.god && !replay.active();
    Loaded g;
    if (!load_level_data(g, opts, internal, carry, restore_in, god_active,
                         display.surface().use_hd_text()))
        return LevelOutcome::kQuit;
    // --level is the display level; level rules use the internal one (slots 3
    // and 5 swap, kGameLevelOrder).  Log both so a capture names what it shows.
    std::fprintf(stderr, "game: level %d (internal %d)\n", display_level,
                 internal);

    // Hold-to-swing pacing; the cooldown is re-read per frame (Options apply
    // live).
    Autofire autofire;

    // --cheats power-up picker (F7; UP/DOWN; ENTER or 1-6 grants; ESC closes).
    // Pauses the world while open.
    CheatPicker cheats;
    LevelFx fx;
    bool running = true;

    // ESC / window close: abort to the title via the game-over path.  Separate
    // from g.state.game_over, which --god resets every frame.
    bool abort_to_title = false;

    // In-game Pause menu (ESC): model generated from data/menus.json, freezes
    // the sim like the cheat picker.
    bool want_reinit = false;   // Pause → an Apply that rebuilds the display
    DisplaySettings reinit_req;     // ... into this pipeline
    const std::optional<MenuModel> menu_model_opt =
        load_menu_model(opts.profile_family);
    MenuModel pause_model = menu_model_opt.value_or(MenuModel{});
    const bool menu_ok = menu_model_opt.has_value();
    // Unreachable: the model is generated at build time.  Kept because every
    // call site takes optional<>.
    if (!menu_ok) {
        std::fprintf(stderr, "menu: no menu model - ESC falls back to "
                     "quit-to-title\n");
    }

    // want_reinit/reinit_req stay locals: the REINIT_TEST hook and the
    // rebuild below use them.
    PauseService pause(pause_model, menu_ok,
                       {&g, &replay, &opts, &pipe, &god_active,
                        &abort_to_title, &out_load, &want_reinit, &reinit_req,
                        display_level});

    // F5 bug-report form: freezes the sim; tag/repro rows + a multi-line
    // description.  Leaving it asks Save (report with annotations) or Discard
    // (drop the capture).
    ReportFormService report_form(pause_model);

    // Dev instrumentation and the OLDUVAI_* hooks (diag/level_diag.hpp).
    LevelDiag diag;
    open_pause_shot(pause, menu_ok, diag.hooks);
    diag.stats.begin_run();
    // OLDUVAI_MENU_SCRIPT: headless menu walk, one token per frame, through the
    // gamepad's SDL_PushEvent path (tests/menu_script.sh).  Tokens: esc up down
    // left right enter space 1..6 | wait | shot | quit.
    diag.menu.load_from_env();

    int frame = 0;
    const Uint32 frame_ms = 1000 / 18;   // 18 Hz logic (aux pacing sites)
    TickPacer pacer;   // the end of each tick (window_util.hpp)

    // The level's presentation.  It reads the menus; they do not read it.
    display.open_view({opts, g, fx, diag, cheats,
                       [&] {
                           return pause.open() || report_form.open() ||
                                  cheats.open();
                       },
                       frame_ms});
    LevelOutcome outcome = LevelOutcome::kQuit;

    // Level-entry loading screen.
    if (!display.view().screen().text_screen(
            display.view().text_deps(), display.surface().use_hd_text(),
            "OLDUVAI_DUMP_LOADING", "loading", [&](const TextScreenHd& hd) {
                return show_loading_screen(
                    nullptr, display_level,
                    {g.charset, g.render.palette, display.view().present(), hd});
            })) {
        running = false;
    }

    // Level music starts after the loading screen, with the level (as the
    // reference does).
    const auto start_music = [&] {
        play_level_music(pipe.audio.get(), opts.game_dir, internal);
    };
    if (running) start_music();

    // --debug-perf: rolling FPS / frame time over ~30 frames.  last_t: wall
    // clock at the previous frame top; ms_accum: per-frame compute time.
    const bool any_debug_overlay =
        opts.debug_collision || opts.debug_entities || opts.debug_perf;
    diag.perf.last_t = SDL_GetTicks();

    // OLDUVAI_REINIT_TEST: after the in-place rebuild, write the round-trip
    // result and end the loop.
    ReinitTestHook reinit_hook(std::getenv("OLDUVAI_REINIT_TEST"));

    // A display or audio Apply, in place: the level stays and the pause stays
    // open over the new display.  The level's data is rebuilt from the game
    // files and a snapshot — the checkpoint restore — because a Style change
    // alters bind-time data (the backdrop extension, the erased HUD labels).
    // The music plays on unless the audio was replaced.  False: the pipeline
    // could not be rebuilt (the program ends, as the session's adopt says).
    const auto reinit_in_place = [&] {
        const SaveState snapshot = capture_save(g, display_level);
        const Rebuilt r = display.rebuild(
            reinit_req, /*aspect=*/nullptr, [&](bool use_hd_text) {
                g = Loaded{};
                return load_level_data(g, opts, internal, carry, snapshot,
                                       god_active, use_hd_text);
            });
        if (r == Rebuilt::kFailed) return false;
        pause.pipeline_changed();
        want_reinit = false;
        pacer.renderer_changed();
        if (r == Rebuilt::kAudioToo) start_music();
        reinit_hook.maybe_write_result(g.state, sw.win, opts, running);
        return true;
    };

    // Player position of the last presented frame, for the level-end fade (set
    // by the 8b intercept).
    int end_px = 0;
    int end_py = 0;
    while (running) {
        // Bound per frame: a reinit replaces both, then starts the next one.
        const LevelSurface& surface = display.surface();
        LevelView& view = display.view();
        cursor_autohide_frame();   // keyboard game: park the OS arrow
        pause.begin_frame();

        diag.stats.begin_tick();
        const Uint32 t0 = SDL_GetTicks();
        diag.sample_perf(any_debug_overlay);

        // OLDUVAI_REINIT_TEST: frame-5 pre-reinit trigger
        // (reinit_test_hook.hpp).
        reinit_hook.maybe_trigger(g.state, opts, frame, menu_ok, reinit_req,
                                  want_reinit, pause);
        // Before this frame's movement: position (direction inference) and
        // whether the player was inside a cave/secret.
        const PrevFrame pf(g.state);

        // Consume one token before the poll, so this frame's event loop handles
        // it.
        if (diag.menu.active() && drive_menu_script(diag.menu, &report_form)) {
            outcome = LevelOutcome::kQuitProgram;
            break;
        }
        poll_level_events(sw.win, report_form, pause, cheats, g.state,
                          opts.cheats && !replay.active(), abort_to_title);

        // Leaving Options with staged changes opens the confirm dialog.
        pause.track_options_exit();

        // F5 form and Pause freeze the sim and draw over the frozen scene;
        // `continue` skips the frame counter, run_frame, post-frame logic and
        // render.  The two are exclusive (F5 fires only outside pause).
        if (report_form.open() &&
            report_form.service_freeze(view.report_deps(
                god_active, display_level, internal)))
            continue;
        const PauseService::FreezeResult pfr = pause.verdict();
        if (pfr == PauseService::FreezeResult::kFroze) {
            view.fp().present_paused(pause.menu(), pause.confirm(),
                                      diag.hooks.pause_shot);
            // A pause shot quits the program, not the level: the sequencer
            // would move on and the shot never exit.
            if (diag.hooks.pause_shot != nullptr) {
                outcome = LevelOutcome::kQuitProgram;
                break;
            }
            SDL_Delay(frame_ms);
            continue;
        }
        if (pfr == PauseService::FreezeResult::kReinitDisplay) {
            if (reinit_in_place()) continue;
            outcome = LevelOutcome::kQuitProgram;
            break;
        }
        if (pfr != PauseService::FreezeResult::kNone) {
            outcome = pause_outcome(pfr, pause, out_warp_display);
            break;
        }

        if (!cheats.open()) systems::wrap_frame_counter(g.state, god_active);

        // This frame's inputs: replay, or live keyboard + gamepad + autofire.
        const systems::FrameInputs in = gather_frame_inputs(
            replay, frame, opts.autofire, autofire, g.state.player, running);
        // Record the resolved inputs at frame+1: the reader resolves
        // replay.at(frame+1).
        if (input_rec.active()) input_rec.record(frame + 1, in);

        // Previous-tick snapshot for the smooth-motion lerp, before the sim
        // tick.
        save_prev_positions(g.state);

        systems::set_bird_bounds(
            g.state,
            bird_margin(view.wsp(), replay.active() || trace.active()));
        // The cheat picker freezes the world (systems::run_tick).
        systems::run_tick(g.state, in, /*paused=*/cheats.open());
        // Margin monsters: cycle walk sprites in place once per tick.  No
        // movement, RNG or collision; L3A alternates follow the global phase
        // counter.
        if (!cheats.open() && view.wsp().active())
            view.wsp().tick_margin_monsters();
        systems::end_tick(g.state, god_active);   // god refill, steps 6b-8a
        // OLDUVAI_FORCE_LEVEL_COMPLETE=<frame>: fire the intercept below
        // headlessly.
        if (frame == diag.hooks.force_level_complete)
            g.state.level_complete = true;

        // 8b. Level complete: leave the loop (FUN_21f3_006f +0x01a1 jumps to
        // its exit block).  The pseudo-exit screen is never bound, composed,
        // presented or traced.  Fade and tally run after the loop, as in
        // boss_app.
        if (g.state.level_complete) {
            g.state.screen_change = false;
            end_px = pf.px;
            end_py = pf.py;
            break;
        }
        if (g.state.game_over || abort_to_title) {
            // MORT.MDI + THEEND.PC1 play in run_game's post-loop game-over
            // sequence (FUN_2bd7_02e7), shared with boss deaths.
            outcome = LevelOutcome::kGameOver;
            running = false;
        }

        // 9. Screen change: per-screen state clear, rebind, one-frame gameplay
        // skip (sequence/screen_change.hpp).
        if (g.state.screen_change)
            change_screen(g, pf, view.trans(), view.fp(),
                          view.fb(), opts.enhanced,
                          view.descent(running));

        play_pending_sfx(g.state, *pipe.audio);

        const bool fluid_bubbles = opts.enhanced && g.fluid_bubbles_initialized;
        secret_room_pass(g, fluid_bubbles);
        std::function<void(RenderTarget&)> bubble_hook;
        if (fluid_bubbles && g.state.secret_flag)
            bubble_hook = make_bubble_hook(g, view.wsp().active());
        fx.step(g);
        // Before the smooth-motion save/restore, so the advance survives.
        // wsp.present draws entities again without advancing; fb itself shows
        // only on non-widescreen paths (pause, transitions, screenshot).
        view.tick().compose(bubble_hook);
        view.tick().advance_once(view.banners());

        // Transition playback, after the new screen's first compose.
        // Surface<->surface: the CRTC pan (2bd7 wipe family; classic 12 frames
        // at 18 Hz).  Cave/secret enter/exit and the L3/L7 warp: the palette
        // fade pair (FUN_1052_0c15).  The HUD pans with the screen: both
        // buffers carry their own.
        view.play_transition(pf, bubble_hook,
                             {&running, diag.draw_log.get(), frame_ms,
                              opts.enhance.smooth_motion});

        maybe_auto_fullscreen(sw.win, frame);

        // Before the present-path selection, so a resize or an Aspect edit
        // shows this frame instead of waiting for the next transition.
        view.wsp().sync_output();
        const bool smooth_vsync_ran =
            view.tick().present(pf, bubble_hook, fluid_bubbles, frame);

        // Post-render snapshot, matching the reference's frame-top capture.
        if (trace.active()) trace.write(frame, g.state);

        // The reference also decrements after display.flip().
        systems::tick_cave_emerge(g.state);

        ++frame;
        if (!opts.screenshot.empty() && frame == opts.screenshot_frame) {
            view.tick().capture_shot(opts.screenshot, surface.hd(), sw.ren,
                                     bubble_hook);
            running = false;
        }
        if (frame_limit_reached(opts, frame, diag.hooks)) running = false;

        const Uint32 spent = SDL_GetTicks() - t0;
        if (any_debug_overlay) diag.perf.ms_accum += spent;
        diag.stats.end_tick();

        pacer.end_tick(sw.ren, surface.tex(), smooth_vsync_ran, opts.vga_scan,
                       surface.hd());
    }

    // The 8b break skipped the game-over check: a game over on the completing
    // frame wins.
    if (g.state.level_complete) {
        if (g.state.game_over || abort_to_title)
            outcome = LevelOutcome::kGameOver;
        else
            outcome = play_platform_ending(
                          {g, display.view().wsp(), sw.win,
                           display.view().fb(), display.view().screen(),
                           display.view().text_deps(),
                           display.view().present(),
                           pipe.audio.get(), opts.game_dir, opts.enhanced,
                           frame_ms, display.surface().use_hd_text(),
                           display_level},
                          end_px, end_py)
                          ? LevelOutcome::kComplete
                          : LevelOutcome::kQuitProgram;
    }

    diag.report_vga_pace(pacer);
    diag.stats.report(display_level);
    pipe.audio->stop_music();
    carry.lives = g.state.player.lives;
    carry.score = g.state.score;
    return outcome;
}

}  // namespace olduvai::presentation
