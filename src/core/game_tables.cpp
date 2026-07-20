// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "core/game_tables.hpp"

namespace olduvai::core {

namespace {
GameTables g_tables;
}  // namespace

const GameTables& game_tables() { return g_tables; }

void install_game_tables(const GameTables& t) { g_tables = t; }

}  // namespace olduvai::core
