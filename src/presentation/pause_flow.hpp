// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// In-game Pause Options SettingsFlow — the staging/confirm/apply controller
// wiring, extracted from run_platform_level (CC2c, incremental). The hooks
// capture a PauseFlowDeps* by value (pointers into run_platform_level's locals,
// which outlive the flow) — NOT the builder's reference params, which would
// dangle on return. Exercised by the menu_script `menu_settings` scenario.

#pragma once

#include <optional>

#include "presentation/confirm_dialog.hpp"   // ConfirmDialog
#include "presentation/game_app.hpp"         // GameOptions
#include "presentation/level_save.hpp"       // PendingReinit
#include "presentation/level_state.hpp"      // Loaded
#include "presentation/menu.hpp"             // Menu, MenuActionTable
#include "presentation/menu_model.hpp"       // MenuModel
#include "presentation/pause_bindings.hpp"   // PauseBindings
#include "presentation/replay.hpp"           // InputReplay
#include "presentation/save_state.hpp"       // SaveState
#include "presentation/settings_flow.hpp"    // SettingsFlow
#include "presentation/settings_session.hpp" // SettingsSession
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
std::optional<MenuModel> load_pause_menu_model();

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
    int* logical_w;
    int* logical_h;
    int hd_scale;
    int display_level;
};

void configure_pause_bind(PauseBindings& bind, const PauseBindWireDeps& d);

}  // namespace olduvai::presentation
