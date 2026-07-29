// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Resolve one gameplay frame's inputs.  A replay session reads the recorded
// frame+1 (the reference injects keys for frame+1 after tracing frame N) and
// ends the run past last_frame+18; a live session reads the keyboard + gamepad,
// with autofire pulsing the attack from the pre-frame club/latch state.
#pragma once

#include <string>

#include "presentation/input/autofire.hpp"    // Autofire, autofire_cooldown
#include "presentation/input/replay.hpp"      // Replay
#include "systems/frame_runner.hpp"     // systems::FrameInputs
#include "systems/player.hpp"           // systems::PlayerState

namespace olduvai::presentation {

systems::FrameInputs gather_frame_inputs(InputReplay& replay, int frame,
                                         const std::string& autofire_mode,
                                         Autofire& autofire,
                                         const systems::PlayerState& player,
                                         bool& running);

}  // namespace olduvai::presentation
