// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Enhanced: the balloon bunch floats away when the player lets go of it —
// the L1 balloon ride landing on screen 12, and the boss arena fly-in.  In
// the original the balloons simply vanish at that moment; this reuses the
// death halo's sprite (kSprBalloonBunch) and rise (kBalloonRisePerTick) so the
// two read as the same object.  Presentation only: no SystemsState, no RNG,
// so the oracle traces cannot see it, and Classic never arms it.
#pragma once

#include <vector>

#include "formats/mat.hpp"
#include "presentation/render/game_render.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

class RisingBalloons {
public:
    // Once per logic tick, BEFORE that tick's frame is drawn (the death halo's
    // order).  `holding`: the player still carries the balloons this tick.
    // `bx`/`by`: where the bunch is drawn while held.  The bunch is released
    // on the tick `holding` goes false, unless `released` is false (a death,
    // whose balloons the game itself sends up).  `screen` ends the effect on
    // a screen change — the balloons belong to the screen they left from.
    void step(bool enabled, bool holding, bool released, int bx, int by,
              int screen) {
        if (active_) {
            prev_y_ = y_;
            y_ -= systems::kBalloonRisePerTick;
            if (y_ < -kOffTop || screen != screen_) active_ = false;
        }
        if (enabled && was_holding_ && !holding && released) {
            active_ = true;
            x_ = bx;
            y_ = prev_y_ = by;
            screen_ = screen;
        }
        was_holding_ = holding;
    }

    // `alpha` interpolates the rise on smooth-motion sub-frames (1 = the
    // tick's own position).
    void draw(RenderTarget& t, const std::vector<formats::Sprite>& atlas,
              const std::vector<formats::Rgb>& pal, float alpha = 1.0f) const {
        if (!active_ ||
            kSprBalloonBunch >= static_cast<int>(atlas.size()))
            return;
        const float fy =
            static_cast<float>(prev_y_) +
            static_cast<float>(y_ - prev_y_) * alpha;
        blit_sprite(t, atlas[static_cast<std::size_t>(kSprBalloonBunch)], pal,
                    static_cast<float>(x_), fy);
    }

    bool active() const { return active_; }

private:
    static constexpr int kOffTop = 80;   // taller than the sprite
    bool active_ = false;
    bool was_holding_ = false;
    int x_ = 0, y_ = 0, prev_y_ = 0, screen_ = 0;
};

}  // namespace olduvai::presentation
