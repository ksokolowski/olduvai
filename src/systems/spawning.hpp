// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Entity spawning — builds live entities from the prepared spawn records
// (per-type initialisation as performed at screen load).  // FUN_27f7_02cb
//
// Consumes raw {type, words} records (prepare/exe_tables) plus the 48-byte
// monster data tables.  LCG-consuming inits (fish dy, jumping-fish dy + x,
// chimp-L7 dy, pteriyaki x/wing/subcounters, spider counter, snake counter)
// advance the shared global LCG in original call order.

#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "core/types.hpp"
#include "prepare/exe_tables.hpp"

namespace olduvai::systems {

struct MonsterTables {
    // type id → main table; L3A also carries the alternate-phase table.
    std::map<int, prepare::MonsterTable> main;
    std::map<int, prepare::MonsterTable> alt;

    static MonsterTables from_exe(const std::vector<std::uint8_t>& exe);
};

// Spawn all entities for one screen's record list.  `cave_index` >= 0 for
// cave screens (bat bounds come from the cave width table).
std::vector<core::Entity> spawn_screen_entities(
    const std::vector<prepare::ObjectRecord>& records,
    const MonsterTables& tables, int cave_index = -1);

}  // namespace olduvai::systems
