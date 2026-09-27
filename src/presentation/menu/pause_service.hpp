// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// In-game Pause: per-frame service glue.  Owns the pause state (open flag,
// exit intents, bindings, staging session, confirm dialog, menu, SettingsFlow)
// and its input routing; the controllers live in pause_flow.cpp /
// pause_bindings.hpp.  It draws nothing: the verdict is an intent the caller
// maps to a LevelOutcome, and the presenter draws the menu.  Ordering is part
// of the frame-loop contract (pause_shot / menu_script / reinit_smoke).

#pragma once

#include <SDL.h>

#include <optional>

#include "presentation/menu/confirm_dialog.hpp"
#include "presentation/level/level_save.hpp"
#include "presentation/level/level_state.hpp"
#include "presentation/menu/menu.hpp"
#include "presentation/menu/pause_flow.hpp"
#include "presentation/menu/pause_routing.hpp"
#include "presentation/input/replay.hpp"
#include "presentation/menu/settings_flow.hpp"
#include "presentation/menu/settings_session.hpp"
#include "presentation/pipeline.hpp"

namespace olduvai::presentation {

class PauseService {
public:
    // Long-lived locals of run_platform_level (the wiring/action closures
    // capture these pointers, exactly as the inline setup did).
    struct External {
        Loaded* g;
        InputReplay* replay;
        GameOptions* opts;
        Pipeline* pipe;       // an adopt replaces its window and audio
        bool* god_active;
        bool* abort_to_title;
        std::optional<SaveState>* out_load;
        bool* want_reinit;
        DisplaySettings* reinit_req;
        int display_level;
    };

    PauseService(MenuModel& model, bool menu_ok, const External& x);

    bool open() const { return open_; }
    // Raw toggle for the OLDUVAI_REINIT_TEST hook (opens the freeze without
    // opening the menu — the intent mapping fires before any draw).
    void set_open(bool v) { open_ = v; }
    // OLDUVAI_PAUSE_SHOT: force-open a menu screen (unknown id → no overlay).
    void force_open_screen(const char* screen);

    // Present-path reads (upload_and_show's HD vector-glyph pass).
    const Menu& menu() const { return menu_; }
    const ConfirmDialog& confirm() const { return confirm_; }

    // At the top of the loop: pause closed since last frame with a dirty
    // session = Discard (Apply already empties it).
    void begin_frame();

    // Pause menu owns input while open; swallows every gameplay key.
    void handle_keydown(SDL_Keycode sym);

    // Gameplay ESC: open the overlay (menus.json loaded) or fall back to a
    // direct title-abort.
    void esc_pressed();

    // After input: leaving Options with staged changes opens the confirm
    // dialog.
    void track_options_exit();

    // This frame's verdict, in this order: quit / restart / load / warp /
    // reinit / abort.  kFroze: the overlay is up; the caller presents the
    // paused frame (FramePresenter::present_paused) and skips the tick.
    enum class FreezeResult {
        kNone,             // pause closed — the frame proceeds
        kFroze,            // pause open, no intent
        kQuitProgram,      // Pause → Exit Game
        kRestartLevel,     // Pause → Restart Level
        kLoadCheckpoint,   // Pause → Load Game (out_load already set)
        kWarpLevel,        // Cheats → Warp! (want_warp() has the target)
        kReinitDisplay,    // an Apply the driver rebuilds the display for
        kAbortGameOver,    // Quit to Title via the game-over path
    };
    FreezeResult verdict() const;

    int want_warp() const { return want_warp_; }

    // After the driver rebuilt the display in place: the settings the menu
    // previews into and compares against follow the adopted pipeline.
    void pipeline_changed();

private:
    int wire_bind_(const External& x);   // ordering shim (see ctor)

    External x_;
    bool menu_ok_;
    bool open_ = false;
    bool want_quit_program_ = false, want_restart_ = false, want_load_ = false;
    int want_warp_ = 0;
    PauseBindings bind_;
    SettingsSession session_;
    ConfirmDialog confirm_;
    PauseActionsDeps actions_deps_;
    // configure_pause_bind must run before the Menu is constructed; this member
    // keeps that order in the initializer sequence.
    int bind_wired_;
    Menu menu_;
    PauseFlowDeps flow_deps_;
    SettingsFlow flow_;
    PauseRouting routing_{menu_, flow_, session_, confirm_, open_};
};

}  // namespace olduvai::presentation
