// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/input/frame_input.hpp"

#include <SDL.h>

#include "presentation/input/gamepad.hpp"

namespace olduvai::presentation {

systems::FrameInputs gather_frame_inputs(InputReplay& replay, int frame,
                                         const std::string& autofire_mode,
                                         Autofire& autofire,
                                         const systems::PlayerState& player,
                                         bool& running) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    systems::FrameInputs in;
    if (replay.active()) {
        // The reference reads key state for the NEXT frame (its oracle injects
        // keys for frame+1 after tracing frame N) — match it or every input
        // lands one frame late.
        in = replay.at(frame + 1);
        if (frame > replay.last_frame() + 18) running = false;
    } else {
        in.left = keys[SDL_SCANCODE_LEFT] != 0 || gamepad::left();
        in.right = keys[SDL_SCANCODE_RIGHT] != 0 || gamepad::right();
        in.up = keys[SDL_SCANCODE_UP] != 0 || gamepad::up();
        in.down = keys[SDL_SCANCODE_DOWN] != 0 || gamepad::down();
        const bool attack_held = keys[SDL_SCANCODE_SPACE] != 0 ||
                                 keys[SDL_SCANCODE_LCTRL] != 0 ||
                                 gamepad::attack_held();
        // Autofire reads the PRE-frame latch/club state — exactly what this
        // frame's latch check will see — and must stay ahead of input_rec so
        // recordings hold the resolved pulses.
        autofire.cooldown = autofire_cooldown(autofire_mode);
        in.attack = autofire.attack(attack_held, player.club_flag,
                                    player.attack_latch);
    }
    return in;
}

}  // namespace olduvai::presentation
