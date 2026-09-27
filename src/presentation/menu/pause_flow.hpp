// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// In-game Pause Options wiring (SettingsFlow).  Hooks capture a PauseFlowDeps*
// by value (pointers to run-loop locals that outlive the flow), never the
// builder's reference parameters, which would dangle.  Exercised by the
// menu_script `menu_settings` scenario.

#pragma once

#include <functional>
#include <optional>

#include "presentation/menu/confirm_dialog.hpp"   // ConfirmDialog
#include "presentation/game_app.hpp"         // GameOptions
#include "presentation/level/level_state.hpp"      // Loaded
#include "presentation/menu/menu.hpp"             // Menu, MenuActionTable
#include "presentation/menu/menu_model.hpp"       // MenuModel
#include "presentation/menu/pause_bindings.hpp"   // PauseBindings
#include "presentation/input/replay.hpp"           // InputReplay
#include "presentation/level/save_state.hpp"       // SaveState
#include "presentation/menu/settings_flow.hpp"    // SettingsFlow
#include "presentation/menu/settings_session.hpp" // SettingsSession
#include "presentation/window_util.hpp"      // ScaledWindow

namespace olduvai::presentation {

// Stable pointers into run_platform_level's locals. The instance lives on that
// frame; make_pause_flow's hooks capture the PauseFlowDeps* by value.
struct PauseFlowDeps {
    Menu* menu;
    GameOptions* opts;
    PauseBindings* bind;
    DisplaySettings* reinit_req;   // the pipeline a reinit Apply adopts
    bool* want_reinit;
};

// Load the full menu model: the on-disk locations in order, then the
// compiled-in copy, so a lone binary still has menus.  Use this rather than
// searching by hand.
// The menu model, its button rows labelled as the connected pad prints them
// (as the session's family prints them when SDL does not know the pad).
std::optional<MenuModel> load_menu_model(const std::string& profile_family);

SettingsFlow make_pause_flow(MenuModel& model, SettingsSession& session,
                             ConfirmDialog& confirm, PauseFlowDeps* d);

// Deps for the pause action closures (resume / quit / restart / cheats / save /
// load): pointers to run-loop locals, captured by value.
struct PauseActionsDeps {
    Loaded* g;
    InputReplay* replay;
    PauseBindings* bind;
    GameOptions* opts;
    std::optional<SaveState>* out_load;
    bool* pause_open;
    bool* abort_to_title;
    bool* want_quit_program;
    bool* want_restart;
    bool* want_load;
    bool* god_active;
    int* want_warp;
    int display_level;
    ConfirmDialog* confirm;   // the quit actions ask "Are you sure?" first
};

MenuActionTable make_pause_actions(PauseActionsDeps* d);

// Deps for wiring the pause PauseBindings instance to run_platform_level's
// live state. The apply_aspect closure captures the stable pointers by value.
struct PauseBindWireDeps {
    bool* god_active;
    SdlAudio* audio;
    const ScaledWindow* sw;
    GameOptions* opts;
    SettingsSession* session;
    int display_level;
};

void configure_pause_bind(PauseBindings& bind, const PauseBindWireDeps& d);

}  // namespace olduvai::presentation
