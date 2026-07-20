// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SDL2 game-controller support with configurable button mapping.
//
// Design (two prongs, because the codebase reads input two ways):
//  1. A single global SDL event watch — registered with a NAMED function
//     (SDL_DelEventWatch matches by pointer; lambdas would leak) — sees
//     every event regardless of which loop polls the queue.  It handles
//     controller hotplug and translates the mapped buttons into
//     synthetic keyboard events (dpad → arrows, confirm → RETURN,
//     back/pause → ESCAPE), so every existing event-driven loop (title
//     menu, pause, options, intro cards, tally pauses, confirm dialogs)
//     works with a pad unmodified.
//  2. Polled accessors (dpad OR left stick past a deadzone; mapped
//     buttons) for the few SDL_GetKeyboardState sites — gameplay input
//     builds and the skip-held helpers — which synthetic events cannot
//     reach (they don't alter the keyboard state array).
//
// Mapping comes from play.json (pad_jump / pad_attack / pad_pause /
// pad_confirm / pad_back, values are SDL button names: "a", "b", "x",
// "y", "start", "back", "leftshoulder", ... — parsed by SDL itself),
// plus pad_deadzone (axis units, default 8000).

#pragma once

#include <SDL.h>

#include <string>

namespace olduvai::presentation::gamepad {

struct Config {
    SDL_GameControllerButton jump = SDL_CONTROLLER_BUTTON_A;
    SDL_GameControllerButton attack = SDL_CONTROLLER_BUTTON_X;
    SDL_GameControllerButton pause = SDL_CONTROLLER_BUTTON_START;
    SDL_GameControllerButton confirm = SDL_CONTROLLER_BUTTON_A;
    SDL_GameControllerButton back = SDL_CONTROLLER_BUTTON_B;
    int deadzone = 8000;
};

// Init the controller subsystem, register the event watch, open any
// already-connected controller.  Safe to call once per process (after
// SDL_Init); idempotent.
void init(const Config& cfg);
void shutdown();

bool connected();

// Live polled state — dpad OR left stick (deadzone-gated) OR mapped button.
bool left();
bool right();
bool up();
bool down();
bool jump_held();
bool attack_held();
// confirm OR jump (the "fire" sense used by skip-held helpers).
bool fire_held();

// Parse an SDL button name ("a", "start", "leftshoulder", ...); returns
// `def` and warns on stderr if the name is unknown.
SDL_GameControllerButton button_from_string(const std::string& name,
                                            SDL_GameControllerButton def);

}  // namespace olduvai::presentation::gamepad
