// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Constants parity — values pinned from the reference engine's resolved
// catalog (dumped 2026-06-09).

#include "doctest/doctest.h"
#include "core/constants.hpp"
#include "core/game_tables.hpp"
#include "core/types.hpp"

using namespace olduvai::core;

TEST_CASE("level order: dispatcher swaps 3 and 5; nothing else") {
    CHECK(kGameLevelOrder == std::array<int, 7>{1, 2, 5, 4, 3, 6, 7});
    // Display level 3 (icy land) is internal 5; display 5 (dark woods) is 3.
    CHECK(kGameLevelOrder[2] == 5);
    CHECK(kGameLevelOrder[4] == 3);
}

TEST_CASE("timing + player init parity") {
    CHECK(kGameFps == 18);
    CHECK(kLastScreen == 18);
    CHECK(kInitialEnergy == 10);
    CHECK(kInitialLives == 3);
    CHECK(kJumpYVel == 20);
    CHECK(kGameoverHoldSeconds == 8);
    CHECK(kScoretallyPauseSeconds == 4);
}

TEST_CASE("cave constants parity") {
    CHECK(kCaveFloorY == 168);
    CHECK(kCavePlayfieldTop == 9);
    CHECK(kCaveEnterX == 40);
    // Cave widths themselves are game data: read at startup from the
    // user's executable into game_tables() (reader covered by the
    // synthetic-image cases in test_exe_tables.cpp).
    CHECK(game_tables().cave_sizes.size() == 53);
}

TEST_CASE("monster state machine skips value 2") {
    CHECK(static_cast<int>(MonsterState::Spawn) == 1);
    CHECK(static_cast<int>(MonsterState::HeadingPlayer) == 3);
    CHECK(static_cast<int>(MonsterState::Dead) == 6);
}

TEST_CASE("object type ids match the dispatcher table") {
    CHECK(static_cast<int>(ObjType::AncestorGhost) == 5);
    CHECK(static_cast<int>(ObjType::RedDino) == 10);
    CHECK(static_cast<int>(ObjType::ProjectileL3) == 30);
    CHECK(static_cast<int>(ObjType::MonsterL7B) == 39);
}
