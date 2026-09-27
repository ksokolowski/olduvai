// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The boss arena's energy-bar drain as state: the fight records every drained
// column in both modes, and the shown arena is rebuilt from the loaded one
// plus that record.  A display rebuild mid-fight (Style, scale) relies on it
// to show the bar the fight has.  Synthetic picture: no game files.

#include "doctest/doctest.h"

#include <cstdint>
#include <vector>

#include "presentation/boss/boss_fight.hpp"

using olduvai::presentation::BossAssets;
using olduvai::presentation::BossFight;
using olduvai::presentation::drain_pip_column;
using olduvai::presentation::rebuild_arena_bg;

namespace {

// A 320x200 RGBA picture with no black: every erased pixel shows.
BossAssets synthetic_arena() {
    BossAssets a;
    a.bg_source.resize(320u * 200u * 4u);
    for (std::size_t i = 0; i < a.bg_source.size(); ++i)
        a.bg_source[i] = static_cast<std::uint8_t>(40 + i % 200);
    a.bg = a.bg_source;
    return a;
}

BossFight fight_at(int level) { return BossFight{level, {}, {}, {}, {}, {}}; }

}  // namespace

TEST_CASE("boss arena: classic rebuild equals the drain as it happened") {
    BossAssets live = synthetic_arena();
    BossFight f = fight_at(4);
    for (const int column : {316, 315, 314, 300})
        drain_pip_column(f, live, column, /*paint=*/true);
    CHECK(live.bg != live.bg_source);

    BossAssets rebuilt = synthetic_arena();
    rebuild_arena_bg(rebuilt, f, /*vector_hud=*/false);
    CHECK(rebuilt.bg == live.bg);
}

TEST_CASE("boss arena: a drain under the vector HUD shows after a switch to classic") {
    // Enhanced paints nothing into the arena, but the fight still records.
    BossAssets enhanced = synthetic_arena();
    BossFight f = fight_at(6);
    for (const int column : {317, 316})
        drain_pip_column(f, enhanced, column, /*paint=*/false);
    CHECK(enhanced.bg == enhanced.bg_source);
    CHECK(f.drained_columns == std::vector<int>{317, 316});

    BossAssets classic = synthetic_arena();
    BossFight painted = fight_at(6);
    for (const int column : {317, 316})
        drain_pip_column(painted, classic, column, /*paint=*/true);

    rebuild_arena_bg(enhanced, f, /*vector_hud=*/false);
    CHECK(enhanced.bg == classic.bg);
}

TEST_CASE("boss arena: the vector HUD rebuild is the loaded picture") {
    BossAssets a = synthetic_arena();
    BossFight f = fight_at(2);
    drain_pip_column(f, a, 310, /*paint=*/true);
    rebuild_arena_bg(a, f, /*vector_hud=*/true);
    CHECK(a.bg == a.bg_source);
}
