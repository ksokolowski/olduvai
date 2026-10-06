// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SDL keycode → SettingsFlow::Key bridge, through the action table for the settings/confirm dialog.
//
// Kept OUT of settings_flow.hpp on purpose: that header is pure logic with
// no SDL dependency (see its top comment, "Same policy, one encoding. Pure
// logic: no SDL"). This is the one place that maps raw SDL keysyms onto the
// semantic dialog keys, shared by the surface (game_app) and boss (boss_app)
// pause/menu flows so the mapping cannot drift.
//
// Previously mirrored verbatim in both TUs (per OL-B6); unified here (CC1
// review, thread CC3) without pulling SDL into the pure-logic layer.

#pragma once

#include <SDL.h>

#include <functional>

#include "presentation/menu/binding_capture.hpp"   // capture_keydown
#include "presentation/menu/menu_nav.hpp"       // menu_nav_keydown
#include "presentation/menu/settings_flow.hpp"

namespace olduvai::presentation {

inline SettingsFlow::Key flow_key_from_sym(SDL_Keycode sym) {
    if (key_is(sym, Action::kLeft) || key_is(sym, Action::kUp))
        return SettingsFlow::Key::kPrev;   // left/up → towards Apply
    if (key_is(sym, Action::kRight) || key_is(sym, Action::kDown))
        return SettingsFlow::Key::kNext;   // right/down → towards Discard
    if (key_is(sym, Action::kConfirm))
        return SettingsFlow::Key::kAccept;
    if (key_is(sym, Action::kBack))
        return SettingsFlow::Key::kCancel;
    return SettingsFlow::Key::kNone;
}

// Route ONE key press for a menu that may have its confirm dialog open.
// Returns true when the dialog consumed it.
//
// The three settings menus — the surface pause, the boss pause and the title
// menu — spelled this out identically (shape_clones.py: `is_open ->
// handle_key -> flow_key_from_sym -> is_open` in three files).  They differ
// only in what ESC does at the ROOT screen, which is `on_root_escape`: the
// two pause menus close the overlay, the title menu re-opens "main" because
// there is nothing behind it.
inline bool menu_dialog_keydown(SDL_Keycode sym, const ConfirmDialog& confirm,
                                SettingsFlow& flow, Menu& menu,
                                const std::function<void()>& on_root_escape) {
    // A listening binding row takes the key first (Options -> Controls).
    if (menu.capturing()) {
        capture_keydown(menu, sym);
        return false;
    }
    // The dialog intercepts all input while open; SettingsFlow
    // resolves move/apply/discard/cancel through that site's own hooks.
    if (confirm.is_open()) {
        flow.handle_key(flow_key_from_sym(sym));
        return true;
    }
    if (key_is(sym, Action::kBack)) {
        menu.back();                      // one screen out (Options → root)
        if (!menu.is_open() && on_root_escape) on_root_escape();
    } else {
        menu_nav_keydown(menu, sym);
        begin_capture_if_started(menu);
    }
    return false;
}

}  // namespace olduvai::presentation
