// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// In-game Pause overlay — the per-frame service glue, extracted verbatim
// from run_platform_level (CC3 phase 2, seam 2).  Owns the pause state
// (open flag, exit intents, bindings, staging session, confirm dialog,
// menu, SettingsFlow) and its input/freeze orchestration.  The controllers
// (Menu, SettingsFlow, PauseBindings wiring, actions) live where they
// always did — pause_flow.cpp / pause_bindings.hpp; this is orchestration
// only.  The freeze returns an INTENT (LevelOutcome is file-local to
// game_app.cpp); the caller maps intents to outcomes in the same order the
// old inline block did.  Ordering is part of the frame-loop contract: the
// pause_shot / menu_script / reinit_smoke golden gates prove the
// extraction byte-exact.

#pragma once

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <optional>

#include "presentation/confirm_dialog.hpp"
#include "presentation/game_render.hpp"
#include "presentation/level_save.hpp"
#include "presentation/level_state.hpp"
#include "presentation/menu.hpp"
#include "presentation/pause_flow.hpp"
#include "presentation/replay.hpp"
#include "presentation/settings_flow.hpp"
#include "presentation/settings_session.hpp"

namespace olduvai::presentation {

class PauseService {
public:
    // Long-lived locals of run_platform_level (the wiring/action closures
    // capture these pointers, exactly as the inline setup did).
    struct External {
        Loaded* g;
        InputReplay* replay;
        GameOptions* opts;
        SdlAudio* audio;
        const ScaledWindow* sw;
        bool* god_active;
        bool* abort_to_title;
        std::optional<SaveState>* out_load;
        bool* want_reinit;
        PendingReinit* reinit_req;
        int* logical_w;
        int* logical_h;
        int hd_scale;
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

    // Close-without-apply detection at the top of the loop: pause open last
    // frame, closed now (Resume) with a dirty session = Discard.  APPLY
    // already clears the session, so no double-revert.
    void begin_frame();

    // Pause menu owns input while open; swallows every gameplay key.
    void handle_keydown(SDL_Keycode sym);

    // Gameplay ESC: open the overlay (menus.json loaded) or fall back to a
    // direct title-abort.
    void esc_pressed();

    // §8.6 step 2: after input handling, SettingsFlow checks whether the
    // menu just left the Options subtree and opens the confirm dialog if
    // changes are staged.
    void track_options_exit();

    // The freeze-frame service's verdict — mapped to LevelOutcome by the
    // caller in this exact order (quit/restart/load/warp/reinit/abort),
    // matching the old inline block.
    enum class FreezeResult {
        kNone,             // pause closed — the frame proceeds
        kFroze,            // overlay drawn; caller `continue`s (full freeze)
        kQuitProgram,      // Pause → Quit to Desktop
        kRestartLevel,     // Pause → Restart Level
        kLoadCheckpoint,   // Pause → Load Game (out_load already set)
        kWarpLevel,        // Cheats → Warp! (want_warp() has the target)
        kReinitDisplay,    // settings change needing re-init
        kAbortGameOver,    // Quit to Title via the game-over path
        kShotQuit,         // OLDUVAI_PAUSE_SHOT captured — quit + stop
    };
    struct FreezeDeps {
        Loaded& g;
        bool god_active;
        bool use_hd_text;
        std::uint32_t frame_ms;
        // run_platform_level's upload_and_show(frame, with_hud, do_present).
        const std::function<void(FrameBuffer&, bool, bool)>& upload_and_show;
    };
    FreezeResult service_freeze(const FreezeDeps& d);

    int want_warp() const { return want_warp_; }

private:
    int wire_bind_(const External& x);   // ordering shim (see ctor)

    External x_;
    bool menu_ok_;
    bool open_ = false, was_open_ = false;
    bool want_quit_program_ = false, want_restart_ = false, want_load_ = false;
    int want_warp_ = 0;
    PauseBindings bind_;
    SettingsSession session_;
    ConfirmDialog confirm_;
    PauseActionsDeps actions_deps_;
    // configure_pause_bind ran BEFORE the Menu was constructed in the old
    // inline setup — this shim member preserves that order inside the
    // member-initializer sequence.
    int bind_wired_;
    Menu menu_;
    PauseFlowDeps flow_deps_;
    SettingsFlow flow_;
};

}  // namespace olduvai::presentation
