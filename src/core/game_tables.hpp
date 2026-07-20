// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Runtime gameplay tables read from the user's own game executable at
// startup (prepare/exe_tables: read_cave_size_table / read_secret_score_
// table).  The engine ships none of this data — content policy: table
// contents live in the user's files, only the readers live here.  Before
// install_game_tables() both arrays are zero, which yields inert cave /
// secret-score behaviour; the app never reaches gameplay without game
// files, and the trace harness installs them itself.

#pragma once

#include <array>

namespace olduvai::core {

struct GameTables {
    // Cave interior widths by cave index.  // DS:0x7CE8 / 0x7D14 / 0x7D2E
    std::array<int, 53> cave_sizes{};
    // SECRET_FOOD score by sprite number.  // DS:0x8094
    std::array<int, 10> secret_scores{};
};

const GameTables& game_tables();
void install_game_tables(const GameTables& t);

}  // namespace olduvai::core
