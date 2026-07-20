// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Surface-level gameplay state: the `Loaded` blob + authored per-level config.
//
// Hoisted out of game_app.cpp (CC2a, 2026-07-09) so the level bind/load/compose
// helpers can move to their own TU (level_setup.cpp) — the first wave of
// decomposing the 3,450-line run_platform_level. Types + constexpr data only;
// no logic. See the internal codebase-review notes (CC2).

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/types.hpp"                 // core::Entity
#include "enhance/hd_asset_cache.hpp"     // HdAssetCache
#include "formats/dur.hpp"                // DurLevel
#include "formats/mat.hpp"                // Sprite
#include "formats/pc1.hpp"                // Rgb, Pc1Image
#include "prepare/exe_tables.hpp"         // ObjectScreens, L3CaveLayouts, LevelTiles
#include "presentation/game_render.hpp"   // LevelRenderAssets
#include "systems/fluid_bubbles.hpp"      // FluidBubbleSystem
#include "systems/player.hpp"             // SystemsState
#include "systems/spawning.hpp"           // MonsterTables

namespace olduvai::presentation {

// Authored per-level configuration (parity with the runtime catalog).
struct LevelConfig {
    int internal_id;
    const char* background_pc1;   // nullptr = no visual background
    bool visual_background;
    const char* tile_mats[2];     // up to two, combined in order
    const char* sprite_mat;
    const char* grot_mat;         // cave tiles (nullptr = none yet)
    const char* dur_file;
    std::uint32_t object_table_ds;
    int spawn_x, spawn_y;
};

constexpr LevelConfig kLevels[] = {
    {1, "FOND1.PC1", true, {"ELEML1.MAT", nullptr}, "L1SPR.MAT",
     "GROT1.MAT", "LEVEL1.DUR", 0x33C2, 100, 66},
    {3, nullptr, false, {"ELEML3.MAT", "ELEML3B.MAT"}, "L3SPR.MAT",
     "GROT3.MAT", "LEVEL3.DUR", 0x47EE, 115, 110},
    {5, "FOND5.PC1", true, {"ELEML5.MAT", "GROT5.MAT"}, "L5SPR.MAT",
     "GROT5.MAT", "LEVEL5.DUR", 0x5840, 140, 65},
    {7, "FOND7.PC1", false, {"ELEML7.MAT", nullptr}, "L7SPR.MAT",
     nullptr, "LEVEL7.DUR", 0x74FC, 95, 10},
};

// The L3 main palette — hand-authored approximation (L3 ships no
// background PC1 to take a palette from); the values are the project's
// and appear nowhere in the executable (byte-searched 2026-07-19).
constexpr formats::Rgb kL3Palette[16] = {
    {0,0,0}, {227,162,130}, {162,97,65}, {130,65,32}, {97,65,32},
    {162,0,32}, {227,195,32}, {130,97,65}, {0,97,65}, {32,130,97},
    {0,0,0}, {65,65,65}, {97,97,97}, {162,130,97}, {195,162,130},
    {227,227,227},
};

// Secret-room water palette — hand-authored approximation, same
// provenance note as kL3Palette above.
constexpr formats::Rgb kSecretPalette[16] = {
    {0,0,0}, {108,216,216}, {108,180,216}, {72,144,216}, {72,180,252},
    {108,144,252}, {0,216,216}, {0,0,0}, {0,108,144}, {0,144,180},
    {0,0,0}, {36,108,108}, {72,144,144}, {108,180,180}, {144,216,216},
    {108,252,252},
};

// Effect-event -> VOC names (the sb_dac rows of the release catalog).
struct SfxVoc { const char* id; const char* voc; };
constexpr SfxVoc kSfxVocs[] = {
    {"SFX_HIT", "MASSUE2.VOC"},
    {"SFX_JUMP_APEX", "RESSORT.VOC"},
    {"SFX_GENERIC", "BONUS.VOC"},
};

struct Loaded {
    LevelRenderAssets render;
    std::vector<formats::Sprite> charset;
    // Per-level persistent entity store — populated once at level entry
    // (the original walks every object table with its reset pass there
    // and never again until the next level), so entity state survives
    // every screen transition: kills stay dead, pickups stays taken.
    // Keys: surface screen | 1000+cave | 2000+secret.
    std::map<int, std::vector<core::Entity>> store;
    // In-memory per-asset HD upscale cache — cleared at level teardown.
    enhance::HdAssetCache hd_cache;
    int bound_key = -1;
    std::vector<formats::Sprite> surface_tiles;
    std::vector<formats::Sprite> cave_tiles;
    prepare::ObjectScreens cave_screens;
    prepare::ObjectScreens secret_screens;
    prepare::L3CaveLayouts l3_caves;
    std::vector<formats::Sprite> grot3;
    formats::Pc1Image theend;
    LevelConfig config{};
    systems::SystemsState state;
    prepare::LevelTiles tiles;
    formats::DurLevel dur;
    prepare::ObjectScreens object_screens;
    systems::MonsterTables monster_tables;
    // Enhanced-mode fluid bubbles (secret room only).
    systems::FluidBubbleSystem fluid_bubbles;
    bool fluid_bubbles_initialized = false;
    // Enhanced-only icy-glider sea-level normalisation: when >= 0, the icy
    // level's decorative water (sprite 7, no collision) is rendered at this
    // constant Y across the flight screens instead of the original's per-screen
    // rising/falling sea — see bind_screen.  -1 = disabled (faithful).
    int glider_water_y = -1;
};

}  // namespace olduvai::presentation
