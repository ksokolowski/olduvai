// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// What a platform level's view reads (level/level_view.hpp), shared with the
// tick render it owns.
#pragma once

#include <cstdint>
#include <functional>

namespace olduvai::presentation {

struct Loaded;
struct LevelFx;
struct LevelDiag;
struct GameOptions;
class CheatPicker;

// What a view reads, all of it outliving the view.
struct LevelViewDeps {
    GameOptions& opts;
    Loaded& g;              // loaded, its entry screen bound
    LevelFx& fx;
    LevelDiag& diag;
    const CheatPicker& cheats;
    std::function<bool()> menus_open;   // banners hide under any open menu
    std::uint32_t frame_ms;
};

}  // namespace olduvai::presentation
