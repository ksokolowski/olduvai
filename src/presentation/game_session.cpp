// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// run_game: the session, from the title through the levels to the endings.
#include "presentation/game_app.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include "prepare/game_files.hpp"        // load_game_executable
#include "presentation/audio/audio.hpp"  // SdlAudio (make_unique)
#include "presentation/render/smooth_config.hpp"   // smooth_present_config
#include "prepare/game_archives.hpp"
#include "presentation/boss_app.hpp"
#include "presentation/input/gamepad.hpp"
#include "presentation/level/level_save.hpp"
#include "presentation/level/level_setup.hpp"   // install_exe_game_data, load_sfx_bank
#include "presentation/level/platform_level.hpp"
#include "presentation/menu/settings_apply.hpp"
#include "presentation/pipeline.hpp"
#include "presentation/sequence/end_sequence.hpp"
#include "presentation/title_menu_flow.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::presentation {

namespace {

// The sequencer's next move after a level.
struct SeqStep {
    enum Kind { kNext, kGoto, kStop } kind = kNext;
    int display = 0;          // kGoto: the display level to enter
    bool game_over = false;   // kStop
    bool quit_program = false;
    int rc = 0;
    static SeqStep next() { return {}; }
    static SeqStep go(int d) { return {kGoto, d, false, false, 0}; }
    static SeqStep stop(int rc = 0) { return {kStop, 0, false, false, rc}; }
    static SeqStep game_over_(int rc = 0) { return {kStop, 0, true, false, rc}; }
    static SeqStep quit() { return {kStop, 0, false, true, 0}; }
};

// One run of the game: the window, the audio device, and the sequencer that
// walks the attract, the levels and the endings (FUN_2bd7_04be).  Options
// edits apply through rt_; launch-fixed fields (game_dir, frames, screenshot,
// replay, trace, level) stay on opts_.
class GameSession {
public:
    explicit GameSession(const GameOptions& opts)
        : opts_(opts), rt_(opts),
          single_(opts.frames > 0 || !opts.screenshot.empty()) {}
    ~GameSession() = default;
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    int run() {
        if (!open()) return 1;
        // Sequencer position: 0 = attract, 1..7 = levels (display numbering),
        // 8 = win ending.  Out of range = L1.  Headless and replay runs skip
        // the attract, keeping frame 0 deterministic.
        display_ = (opts_.level >= 0 && opts_.level <= 8) ? opts_.level : 1;
        if (display_ == 0 && (single_ || !opts_.replay.empty() ||
                              !opts_.record_inputs.empty()))
            display_ = 1;
        // OLDUVAI_AUTOLOAD=1: load the quicksave once at startup, skipping
        // intro and menu, to reproduce a saved scene headlessly.  A game-over
        // restart goes to the normal title.
        bool autoload =
            std::getenv("OLDUVAI_AUTOLOAD") != nullptr && !opts_.save_path.empty();
        int rc = 0;
        // Title -> play -> game over / win -> title.  Replay, headless and
        // --record-inputs runs make one pass.
        while (true) {
            rc = attract_pass(autoload);
            autoload = false;
            if (single_ || !opts_.replay.empty() ||
                !opts_.record_inputs.empty() || quit_requested_)
                break;
            carry_ = CarriedState{};
            display_ = 0;
        }
        SDL_DestroyRenderer(sw_.ren);
        SDL_DestroyWindow(sw_.win);
        SDL_Quit();
        return rc;
    }

private:
    // SDL, the gamepad, the EXE tables, the audio device and the window.
    bool open() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "game: SDL init failed: %s\n", SDL_GetError());
            return false;
        }
        gamepad::init_from_options(opts_);
        // Gameplay tables and the AdLib SFX patches come from the user's
        // executable; installed first so the audio backend can pre-render
        // the OPL SFX.  Non-fatal: the level loader reports bad game files
        // with better context.
        try {
            install_exe_game_data(prepare::load_game_executable(opts_.game_dir));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "game data: EXE tables not installed (%s)\n",
                         e.what());
        }
        open_audio();
        std::printf("audio: music backend = %s\n",
                    audio_->active_music_backend().c_str());
        // One window for the session, same logical size in every phase.
        hd_ = hd_active(rt_.enhanced, rt_.hd_profile);
        hd_scale_ = hd_scale_for(rt_.enhanced, rt_.hd_profile, rt_.render_scale);
        sw_ = create_window(hd_scale_);
        if (sw_.ren == nullptr) {
            std::fprintf(stderr, "game: window creation failed: %s\n",
                         SDL_GetError());
            if (sw_.win != nullptr) SDL_DestroyWindow(sw_.win);
            SDL_Quit();
            return false;
        }
        // -f/--fullscreen: desktop fullscreen, the flag Alt+Enter toggles.
        if (rt_.fullscreen && sw_.win != nullptr)
            SDL_SetWindowFullscreen(sw_.win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        return true;
    }

    // The audio device for rt_'s devices, with its SFX bank and mix.  Enhanced
    // mix: SFX polyphony and music ducking; classic: one voice at the
    // original balance.
    void open_audio() {
        audio_.reset();   // tear down the old device and synth handles first
        AudioSetup setup = audio_setup_of(rt_);
        setup.rate = rt_.audio_rate;
        setup.buffer = rt_.audio_buffer;
        setup.midi_port = rt_.midi_port;
        audio_ = std::make_unique<SdlAudio>(setup);
        audio_->set_mix_balance(rt_.enhanced);
        // Non-fatal: no SFX; the level loader reports the real error.
        try {
            const prepare::GameArchives archives(opts_.game_dir);
            load_sfx_bank(*audio_, [&](const std::string& n)
                                       -> const std::vector<std::uint8_t>* {
                return archives.entry(n);
            });
        } catch (const std::exception& e) {
            std::fprintf(stderr, "audio: SFX bank not loaded (%s)\n", e.what());
        }
    }

    // --display-mode cpu: the software renderer.
    ScaledWindow create_window(int scale) const {
        WindowSpec spec;
        spec.logical_w = 320 * scale;
        spec.logical_h = 200 * scale;
        spec.software = rt_.display_mode == "cpu";
        spec.vsync = opts_.vsync;
        spec.aspect = rt_.aspect;
        spec.win_w = opts_.window_w;
        spec.win_h = opts_.window_h;
        return create_scaled_window(spec);
    }

    // The window and renderer at a new scale (the level builds its own
    // texture).  Fullscreen is not rebuilt: only the windowed size depends on
    // the scale, and a rebuild costs seconds of black on macOS (Spaces) on
    // every enhanced<->classic switch.
    bool rebuild_window(int new_scale) {
        const bool fs = window_fullscreen(sw_.win);
        if (fs && sw_.win != nullptr && sw_.ren != nullptr) {
            set_aspect_logical(sw_.ren, new_scale, rt_.aspect);
            return true;
        }
        if (sw_.ren) SDL_DestroyRenderer(sw_.ren);
        if (sw_.win) SDL_DestroyWindow(sw_.win);
        sw_ = create_window(new_scale);
        if (sw_.ren == nullptr) {
            std::fprintf(stderr, "settings: window rebuild failed: %s\n",
                         SDL_GetError());
            return false;
        }
        if (fs && sw_.win)
            SDL_SetWindowFullscreen(sw_.win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        return true;
    }

    // Adopt a display/audio pipeline: a pause or title Apply, or what a boss
    // staged for after the fight.  `aspect`: only the boss carries it (the
    // other menus apply aspect live).  False when the window rebuild failed.
    bool adopt_pipeline(const DisplaySettings& c, const std::string* aspect) {
        const bool style_changed = c.enhanced != rt_.enhanced;
        rt_.enhanced = c.enhanced;
        // smooth_motion derives from `enhanced` (a stale value also
        // suppresses the classic VGA scanout).
        rt_.enhance.smooth_motion = rt_.enhanced && rt_.transitions != "classic";
        const int new_scale =
            hd_scale_for(rt_.enhanced, c.hd_profile, c.render_scale);
        rt_.render_scale = c.render_scale;
        rt_.hd_profile = c.hd_profile;
        bool aspect_changed = false;
        if (aspect != nullptr) {
            aspect_changed = *aspect != rt_.aspect;
            rt_.aspect = *aspect;
        }
        if (c.music_device != rt_.music_device ||
            c.sfx_backend != rt_.sfx_backend) {
            rt_.music_device = c.music_device;
            rt_.sfx_backend = c.sfx_backend;
            open_audio();
        } else if (style_changed) {
            audio_->set_mix_balance(rt_.enhanced);
        }
        if (new_scale == hd_scale_ && !aspect_changed) return true;
        hd_ = hd_active(rt_.enhanced, rt_.hd_profile);
        hd_scale_ = new_scale;
        if (rebuild_window(hd_scale_)) return true;
        std::fprintf(stderr, "settings: aborting after failed window rebuild\n");
        return false;
    }

    // One pass: the title (at position 0), the levels, then the game-over or
    // win ending.  Returns the exit code.
    int attract_pass(bool autoload) {
        // Main menu -> Continue (or the autoload) hands a checkpoint to the
        // first level entry.
        std::optional<SaveState> menu_continue;
        bool autoloaded = false;
        if (autoload) {
            if (auto s = load_from_file(opts_.save_path)) {
                menu_continue = s;
                display_ = s->hdr.level;
                autoloaded = true;
            }
        }
        // Intro, title and main menu only at position 0 (FUN_2bd7_04be slot
        // 0); --level N jumps straight in.
        if (display_ == 0 && !single_ && opts_.replay.empty() && !autoloaded) {
            TitleMenuCtx tmctx{pipe_,    rt_,          opts_,
                               hd_,      hd_scale_,    display_,
                               quit_requested_, menu_continue, autoloaded};
            run_title_menu(tmctx);
        }
        // Start Game (or missing menu assets) enters L1.
        if (display_ == 0) display_ = 1;
        restore_ = std::move(menu_continue);

        const SeqStep end = quit_requested_ ? SeqStep::stop() : play_levels();
        quit_requested_ = quit_requested_ || end.quit_program;

        // The endings present native 320 frames: undo a level's wide logical
        // canvas, which would stretch them (and the intro after).
        set_aspect_logical(sw_.ren, hd_scale_, rt_.aspect);
        if (!quit_requested_ && end.game_over && !single_)
            show_game_over_screen(opts_.game_dir, *audio_, sw_, hd_scale_,
                                  rt_.hd_profile);
        // Win ending: after L7 (display 8) or --level 8 (FUN_2bd7_04be slot
        // 8).
        if (!quit_requested_ && display_ > 7 && end.rc == 0 && !single_)
            show_win_ending(opts_.game_dir, *audio_, sw_, hd_scale_,
                            rt_.hd_profile, rt_.enhance.smooth_motion,
                            quit_requested_);
        return end.rc;
    }

    // The levels from display_ on, until one stops the sequence or L7 ends.
    SeqStep play_levels() {
        static const int kOrder[7] = {1, 2, 5, 4, 3, 6, 7};   // kGameLevelOrder
        while (display_ <= 7) {
            const int internal = kOrder[display_ - 1];
            const SeqStep st = (internal == 2 || internal == 4 || internal == 6)
                                   ? play_boss(internal)
                                   : play_platform(internal);
            if (st.kind == SeqStep::kStop) return st;
            if (st.kind == SeqStep::kGoto) {
                display_ = st.display;
                continue;
            }
            // --record-inputs records one level (the file is reopened "w" per
            // level), level-end sequence included.
            if (single_ || !opts_.record_inputs.empty()) return SeqStep::stop();
            ++display_;
        }
        return SeqStep::next();
    }

    SeqStep play_boss(int internal) {
        // --god: the arena never runs the surface god seed, so carry 99 lives
        // in (the EXE cap).  Energy and damage stay real; off under --replay.
        // rt_.god: the pause Cheats toggle writes it.
        if (rt_.god && opts_.replay.empty()) carry_.lives = 99;
        const BossRunResult r = run_boss_level(rt_, internal, carry_, pipe_);
        if (r.quit_program) return SeqStep::quit();       // Exit Game
        if (r.restart) return SeqStep::go(display_);      // Restart Fight
        // ESC / window close, or a death: game over -> title.  Headless
        // just stops.
        if (r.quit) return single_ ? SeqStep::stop() : SeqStep::game_over_();
        if (!r.survived && !single_) return SeqStep::game_over_();
        return SeqStep::next();
    }

    SeqStep play_platform(int internal) {
        std::optional<SaveState> load_request;
        int warp_display = 0;   // Pause -> Cheats -> Warp! (display level)
        const LevelOutcome outcome =
            run_platform_level(rt_, display_, internal, carry_, pipe_,
                               restore_, load_request, warp_display);
        restore_.reset();   // applied at entry
        switch (outcome) {
            case LevelOutcome::kComplete:
                return SeqStep::next();
            case LevelOutcome::kLoadCheckpoint:
                if (!load_request) break;
                // The saved level, with the checkpoint applied there.
                carry_.lives = load_request->hdr.player.lives;
                // Range-checked in deserialize: fits a 32-bit long.
                carry_.score = static_cast<long>(load_request->hdr.score);
                restore_ = load_request;
                return SeqStep::go(load_request->hdr.level);
            case LevelOutcome::kRestartLevel:
                return SeqStep::go(display_);
            case LevelOutcome::kWarpLevel:
                // A fresh entry at the chosen level; lives and score carry.
                if (warp_display < 1 || warp_display > 7) break;
                return SeqStep::go(warp_display);
            case LevelOutcome::kQuitProgram:
                return SeqStep::quit();             // Pause -> Exit Game
            case LevelOutcome::kGameOver:
                return SeqStep::game_over_(1);
            case LevelOutcome::kQuit:
                return SeqStep::stop(0);
        }
        return SeqStep::stop(1);
    }

    const GameOptions& opts_;
    GameOptions rt_;
    const bool single_;   // --play-frames / --play-shot: one level
    std::unique_ptr<SdlAudio> audio_;   // rebuilt on a device change
    ScaledWindow sw_;
    Pipeline pipe_{sw_, audio_,
                   [this](const DisplaySettings& c, const std::string* aspect) {
                       return adopt_pipeline(c, aspect);
                   }};
    bool hd_ = false;
    int hd_scale_ = 1;
    CarriedState carry_;
    int display_ = 0;
    bool quit_requested_ = false;
    // A checkpoint for the next surface level entry: Continue, Load Game, or
    // a reinit's snapshot.
    std::optional<SaveState> restore_;
};

}  // namespace

int run_game(const GameOptions& opts) {
    // Publish the smooth-present tuning before any frame loop reads it.
    smooth_present_config() = {opts.smooth_subframes, opts.smooth_vsync_off};
    GameSession session(opts);
    return session.run();
}

}  // namespace olduvai::presentation
