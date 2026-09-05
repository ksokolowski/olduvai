// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// In-game Pause Options SettingsFlow — the staging/confirm/apply controller
// wiring, extracted from run_platform_level (CC2c, incremental). The hooks
// capture a PauseFlowDeps* by value (pointers into run_platform_level's locals,
// which outlive the flow) — NOT the builder's reference params, which would
// dangle on return. Exercised by the menu_script `menu_settings` scenario.

#pragma once

#include "presentation/render/logical_size.hpp"

#include <optional>

#include "presentation/menu/confirm_dialog.hpp"   // ConfirmDialog
#include "presentation/game_app.hpp"         // GameOptions
#include "presentation/level/level_save.hpp"       // PendingReinit
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
    PendingReinit* reinit_req;
    bool* want_reinit;
};

// Load the pause menus.json (SDL base-path candidates → compiled-in fallback).
// Empty only if neither on-disk nor built-in model is available.
// Load the menu model: every screen, not just the pause one.  Tries the four
// on-disk locations in order, then falls back to the compiled-in copy so a
// lone binary with no data/ directory still has menus.
//
// Use this rather than open-coding the search.  Three copies of it existed and
// all three had drifted: the main menu tried only three paths and had NO
// embedded fallback, which left its entire Options flow unreachable in any
// build without data/menus.json.
std::optional<MenuModel> load_menu_model();

SettingsFlow make_pause_flow(MenuModel& model, SettingsSession& session,
                             ConfirmDialog& confirm, PauseFlowDeps* d);

// Deps for the pause menu action closures (resume/quit/restart/cheats/save/
// load). Pointers into run_platform_level's locals; make_pause_actions captures
// this PauseActionsDeps* by value.
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
    bool* want_reinit;
    PendingReinit* reinit_req;
    LogicalSize* lsz;
    int hd_scale;
    int display_level;
};

void configure_pause_bind(PauseBindings& bind, const PauseBindWireDeps& d);

}  // namespace olduvai::presentation
