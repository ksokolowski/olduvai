// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The platform level's enhanced effects: the L3 dust tail, the cave-sign
// teleport clouds, the L1 balloons, and the secret room's tile pass and fluid
// bubbles.
#pragma once

#include <functional>
#include <vector>

#include "presentation/level/level_state.hpp"        // Loaded
#include "presentation/render/game_render.hpp"       // RenderTarget
#include "presentation/render/rising_balloons.hpp"

namespace olduvai::presentation {

// Enhanced L3 dust tail: faint puffs for ~2 s on screen 18 after the trunk
// lands (the EXE cuts the smoke at landing).
constexpr int kL3SmokeTailTicks = 36;   // ~2 s at 18 Hz

// The enhanced effects every present path draws over the scene: the L3 dust
// tail, the teleport clouds and the L1 balloons (which float away when the
// ride lands on screen 12).
struct LevelFx {
    int l3_smoke_tail = 0;      // ticks left
    RisingBalloons balloons;
    float alpha = 1.0f;         // this sub-frame's balloon rise

    // Once per tick: balloons held while the L1 ride is on; a death sends up
    // the game's halo.
    void step(const Loaded& g);
    void draw(RenderTarget& t, const Loaded& g) const;
};

// 8c. Secret-room bubble scatter: exactly one 627-draw LCG pass per frame,
// here in the render gate (the reference runs it as logic step 8c).  The
// enhanced fluid bubbles tick after it: their PRNG is separate.
void secret_room_pass(Loaded& g, bool fluid);

// Enhanced secret room: the rising bubbles, drawn before the tiles so the
// floor covers them as they emerge.  `ws_mirror`: widescreen.
std::function<void(RenderTarget&)> make_bubble_hook(const Loaded& g,
                                                    bool ws_mirror);

// The fluid bubbles at a smooth sub-frame; restore_fluid_bubbles puts the
// logic positions back.
struct BubblePos { float x, y; };
std::vector<BubblePos> lerp_fluid_bubbles(Loaded& g, float alpha);
void restore_fluid_bubbles(Loaded& g, const std::vector<BubblePos>& saved);

// OLDUVAI_BUBBLE_TRACE=1: the slowest moving bubble at this sub-frame.
void trace_slow_bubble(const Loaded& g, int frame, int sub, int scale);

}  // namespace olduvai::presentation
