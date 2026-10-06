// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Options -> Controls capture: Enter on a binding row (Menu::activate) makes
// its selected slot listen, and the next key (a Keyboard row) or pad button
// (a Gamepad row) lands there by the row's rules; the shortcut modifier
// (Select) is never taken.  Esc cancels, and so does
// the pad's Back on a Keyboard row, or five seconds passing; Backspace or
// Delete on an alternate slot empties it.  Shared by every settings menu
// through menu_dialog_keydown.
#pragma once

#include <SDL.h>

#include <cstdint>

#include "presentation/input/actions.hpp"   // key_is
#include "presentation/input/gamepad.hpp"   // arm_capture, take_capture
#include "presentation/menu/menu.hpp"

namespace olduvai::presentation {

inline constexpr std::uint32_t kCaptureMs = 5000;

// After a key press moved `menu`: a capture it began starts listening.
inline void begin_capture_if_started(Menu& menu) {
    if (!menu.capturing()) return;
    const Uint32 deadline = SDL_GetTicks() + kCaptureMs;
    menu.set_clock([] { return SDL_GetTicks(); });
    menu.set_capture_deadline(deadline);
    gamepad::arm_capture(deadline);
}

inline void end_capture(Menu& menu) {
    gamepad::disarm_capture();
    menu.cancel_capture();
}

// A key press while `menu` captures; SDLK_UNKNOWN is the pad's wake-up
// (gamepad::feed_capture).
inline void capture_keydown(Menu& menu, SDL_Keycode sym) {
    const bool pad_row = menu.capture_key().rfind("pad_", 0) == 0;
    if (const auto got = gamepad::take_capture()) {
        if (pad_row && gamepad::is_modifier(*got)) {
            gamepad::arm_capture(menu.capture_deadline());   // Select: taken
        } else if (pad_row) {
            menu.finish_capture(*got);
        } else if (holds(split_binding(gamepad::binding("pad_back")), *got)) {
            end_capture(menu);
        } else {
            gamepad::arm_capture(menu.capture_deadline());   // keep listening
        }
        return;
    }
    if (sym == SDLK_UNKNOWN) return;
    if (key_is(sym, Action::kBack)) {
        end_capture(menu);
    } else if ((sym == SDLK_BACKSPACE || sym == SDLK_DELETE) &&
               menu.capture_slot() == 1) {
        end_capture(menu);
        menu.clear_alternate();
    } else if (!pad_row) {
        // The key's position, as play reads it (key_held).
        const char* name = SDL_GetScancodeName(SDL_GetScancodeFromKey(sym));
        if (name != nullptr && *name != '\0') {
            gamepad::disarm_capture();
            menu.finish_capture(name);
        }
    }
}

}  // namespace olduvai::presentation
