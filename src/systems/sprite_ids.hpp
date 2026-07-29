// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Entity sprite indices shared by more than one systems TU.
//
// These were declared three times over — spawning.cpp and monster_ai.cpp each
// kept a file-local copy, and monster_ai.hpp exported a third partial set — so
// the same sprite index had to be kept in sync across three places by hand.
// Only the indices used by 2+ TUs live here; single-use ones stay file-local
// where their behaviour is (per-monster animation frames, bonus smoke, snake
// poses, the L7 dormant pose, …).
//
// Values are 0-based sprite indices into the level's entity sprite set.
#pragma once

#include "core/types.hpp"   // core::ObjType

namespace olduvai::systems {

// Fish (L5 icy water): the rising/falling pair.
constexpr int kSprFishUp = 83;
constexpr int kSprFishDown = 84;

// Jellyfish (L5): first of the 4-pose rise/fall x left/right block (55..58).
constexpr int kSprJFishRiseLeft = 55;

// Hatching egg (L1) and the falling rock hazards (L7 / the L3 variant).
constexpr int kSprEgg = 125;
constexpr int kSprRock = 143;
constexpr int kSprRockL3 = 116;

// Cave/jungle monsters.
constexpr int kSprChimp = 92;
constexpr int kSprSpider = 118;
constexpr int kSprBat = 121;

// Big walking bird, KO'd pose — the L3 screen-clear latch reads this state
// (see the screen_clear_of_monsters gate in monster_ai / the L3 food gate).
constexpr int kSprBirdKo = 43;

// Bonus icon sprites by type (DS:0x8088, 0-based).
constexpr int kBonusSprites[6] = {90, 81, 89, 130, 131, 139};

// The shared-state-machine monsters: the ten obj types that run the common
// monster AI and take club/body collisions.  monster_ai.cpp (is_shared_monster)
// and collisions.cpp (is_monster) each carried a byte-identical copy of this
// truth table, so adding a monster type meant remembering both.
inline bool is_monster(core::ObjType t) {
    switch (t) {
        case core::ObjType::RedDino:
        case core::ObjType::YellowFuzz:
        case core::ObjType::BrownBear:
        case core::ObjType::GreenDino:
        case core::ObjType::MonsterL3A:
        case core::ObjType::MonsterL3B:
        case core::ObjType::MonsterL5A:
        case core::ObjType::MonsterL5B:
        case core::ObjType::MonsterL7A:
        case core::ObjType::MonsterL7B:
            return true;
        default:
            return false;
    }
}

}  // namespace olduvai::systems
