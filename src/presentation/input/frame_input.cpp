// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/input/frame_input.hpp"

#include <SDL.h>

#include "presentation/input/actions.hpp"
#include "presentation/input/gamepad.hpp"

namespace olduvai::presentation {

systems::FrameInputs gather_frame_inputs(InputReplay& replay, int frame,
                                         const std::string& autofire_mode,
                                         Autofire& autofire,
                                         const systems::PlayerState& player,
                                         bool& running) {
    systems::FrameInputs in;
    if (replay.active()) {
        // The reference reads key state for the NEXT frame (its oracle injects
        // keys for frame+1 after tracing frame N) — match it or every input
        // lands one frame late.
        in = replay.at(frame + 1);
        if (frame > replay.last_frame() + 18) running = false;
    } else {
        in.left = key_held(Action::kLeft) || gamepad::left();
        in.right = key_held(Action::kRight) || gamepad::right();
        in.up = key_held(Action::kUp) || key_held(Action::kJump) ||
                gamepad::up();
        in.down = key_held(Action::kDown) || gamepad::down();
        const bool attack_held =
            key_held(Action::kAttack) || gamepad::attack_held();
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
