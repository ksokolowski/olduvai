// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The action table: what a player does, and which keys do it.  Gameplay and
// every screen ask for an action (input/actions.hpp), never a key, so one
// place decides that Keypad Enter confirms or that W moves up.  The text
// editors are the exception: they type, so they read raw keys.
//
// Keys are SDL key names ("Return", "W"), resolved by input/actions.  Pure
// and header-only: no SDL.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "presentation/input/binding_slots.hpp"

namespace olduvai::presentation {

enum class Action {
    kLeft,
    kRight,
    kUp,       // also jumps, as in the original
    kDown,
    kJump,     // a pad button; on the keyboard Up jumps
    kAttack,
    kConfirm,  // menus: accept
    kBack,     // menus: one screen out, or skip
    kPause,    // play: the pause menu
    kQuicksave,
    kQuickload,
    kCheats,
    kBugReport,
    kQuit,     // play: ask "Exit game?" (the pause menu's own question)
};
inline constexpr std::size_t kActionCount =
    static_cast<std::size_t>(Action::kQuit) + 1;

// The keyboard's bindings.  Arrows and WASD move at once; Space both attacks
// in play and accepts in menus, and Esc both pauses and backs out: which one
// applies is the caller's context.
inline const std::vector<std::string>& default_keys(Action a) {
    static const std::vector<std::string> kKeys[kActionCount] = {
        {"Left", "A"},                         // kLeft
        {"Right", "D"},                        // kRight
        {"Up", "W"},                           // kUp
        {"Down", "S"},                         // kDown
        {},                                    // kJump
        {"Space", "Left Ctrl"},                // kAttack
        {"Return", "Keypad Enter", "Space"},   // kConfirm
        {"Escape"},                            // kBack
        {"Escape"},                            // kPause
        {"F6"},                                // kQuicksave
        {"F9"},                                // kQuickload
        {"F7"},                                // kCheats
        {"F5"},                                // kBugReport
        {"F10"},                               // kQuit
    };
    return kKeys[static_cast<std::size_t>(a)];
}

// The play actions the keyboard remaps (Options -> Controls -> Keyboard),
// each a play.json key holding up to two key names.  The menu keys
// (Enter, Space, Esc) stay fixed, so the keyboard always leaves a menu.
struct KeyAction {
    Action action;
    const char* key;
};
inline constexpr KeyAction kKeyActions[] = {
    {Action::kLeft, "key_left"},     {Action::kRight, "key_right"},
    {Action::kUp, "key_up"},         {Action::kDown, "key_down"},
    {Action::kAttack, "key_attack"}, {Action::kPause, "key_pause"},
    {Action::kQuit, "key_quit"},
};

// A key_* setting's default value: its action's keys, comma-separated.
inline std::string default_key_setting(Action a) {
    return join_binding(default_keys(a));
}

}  // namespace olduvai::presentation
