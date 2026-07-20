// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Autofire (hold-to-swing) input synthesis — pure, SDL-free.  The main-game
// attack is edge-triggered by a latch (systems/player.cpp attack initiation;
// the boss arenas have no latch and re-swing while held).  With autofire on,
// holding the key emulates paced mashing: release while a swing is running
// or the latch is still set, then wait `cooldown` idle frames before the
// next press (fast=0 → perfect mash, the boss-arena cadence).  Pacing is in
// 18.2 Hz logic frames, never wall-clock, so replays stay deterministic.
// Lives in presentation so the systems layer stays byte-identical to the
// reference; callers feed the result through the normal input path (and
// input recording), so replays capture the resolved pulses.

#pragma once

#include <string>

namespace olduvai::presentation {

// Speed token → idle-frame cooldown between swings; -1 = off (any unknown
// token — including bool-era config values — reads as off).
inline int autofire_cooldown(const std::string& token) {
    if (token == "fast") return 0;
    if (token == "medium") return 2;
    if (token == "slow") return 4;
    return -1;
}

struct Autofire {
    int cooldown = -1;   // -1 = off; else idle frames required between swings
    int idle = 0;        // consecutive frames the swing gate has been open

    bool attack(bool held, int club_flag, int attack_latch) {
        if (cooldown < 0) return held;
        if (!held || club_flag != 0 || attack_latch != 0) {
            idle = 0;
            return false;
        }
        ++idle;
        return idle > cooldown;
    }
};

}  // namespace olduvai::presentation
