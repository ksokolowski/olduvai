// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Game controller support with configurable mapping, in two parts:
//  1. A global SDL event watch (a named function: SDL_DelEventWatch matches by
//     pointer) handles hotplug and turns mapped buttons into keyboard events
//     (dpad -> arrows, confirm -> RETURN, back/pause -> ESCAPE), so every
//     event-driven loop works with a pad unchanged.
//  2. Polled accessors (dpad or left stick past a deadzone; mapped buttons)
//     for the SDL_GetKeyboardState sites (gameplay input, skip-held checks),
//     which synthetic events do not reach.
// Mapping from play.json: pad_jump / pad_attack / pad_pause / pad_confirm /
// pad_back (SDL button names: "a", "b", "x", "y", "start", "back",
// "leftshoulder", ...) and pad_deadzone (default 8000).

#pragma once

#include <SDL.h>

#include <optional>
#include <string>

#include "presentation/input/button_layout.hpp"   // PadFamily

namespace olduvai::presentation {
struct GameOptions;   // (game_app.hpp) — for init_from_options
}

namespace olduvai::presentation::gamepad {

struct Config {
    SDL_GameControllerButton jump = SDL_CONTROLLER_BUTTON_A;
    SDL_GameControllerButton attack = SDL_CONTROLLER_BUTTON_X;
    SDL_GameControllerButton pause = SDL_CONTROLLER_BUTTON_START;
    SDL_GameControllerButton confirm = SDL_CONTROLLER_BUTTON_A;
    SDL_GameControllerButton back = SDL_CONTROLLER_BUTTON_B;
    int deadzone = 8000;
};

// Init the controller subsystem, register the watch, open connected pads.
// Once per process, after SDL_Init; idempotent.
void init(const Config& cfg);
// Build a Config from a GameOptions' pad_* keys (button_from_string with the
// per-button defaults) and init().  The session's gamepad-setup one-liner.
void init_from_options(const GameOptions& opts);
void shutdown();

bool connected();
// The letters the connected pad prints, when SDL knows its type; nullopt
// with no pad or an unknown one (a handheld's built-in pad usually).
std::optional<PadFamily> printed_family();

// Live polled state — dpad OR left stick (deadzone-gated) OR mapped button.
bool left();
bool right();
bool up();
bool down();
bool jump_held();
bool attack_held();
// confirm OR jump (the "fire" sense used by skip-held helpers).
bool fire_held();

// The live binding of a pad_* key as an SDL button name; "" for another key.
// The Options menu reads the mapping from here, not from the launch options.
std::string binding(const std::string& key);
// Rebind the action a pad_* key names, from now on.  False for another key
// or an unknown button name (the binding stays).
bool apply_binding(const std::string& key, const std::string& button);

// Parse an SDL button name ("a", "start", "leftshoulder", ...); returns
// `def` and warns on stderr if the name is unknown.
SDL_GameControllerButton button_from_string(const std::string& name,
                                            SDL_GameControllerButton def);

}  // namespace olduvai::presentation::gamepad
