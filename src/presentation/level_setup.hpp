// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Surface-level asset loading, screen binding and static composition.
//
// The bind/load/compose helpers moved verbatim out of game_app.cpp (CC2a-2,
// 2026-07-09) — the second step of decomposing the 3,450-line
// run_platform_level. Only the functions run_platform_level/run_game actually
// call are declared here; the rest (store_key, bind_store, load_level_impl)
// stay file-local in level_setup.cpp. See the internal codebase-
// review notes (CC2a).

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "presentation/audio.hpp"        // SdlAudio
#include "presentation/level_state.hpp"  // Loaded, LevelConfig (+ render types)

namespace olduvai::presentation {

const char* level_music_name(int internal);

// Read the runtime gameplay tables (cave widths, secret scores) and the
// AdLib SFX voice patches from the user's executable bytes and install
// them (core::install_game_tables + install_adlib_sfx_voices).  Idempotent;
// called at app start and again on every level load.
void install_exe_game_data(const std::vector<std::uint8_t>& exe);

void load_sfx_bank(SdlAudio& audio,
                   const std::function<const std::vector<std::uint8_t>*(
                       const std::string&)>& entry);

void refresh_secret_tiles(Loaded& g, bool draw_scatter);

void bind_screen(Loaded& g, int screen);

void build_surface_screen_assets(const Loaded& g, int screen,
                                 presentation::LevelRenderAssets& ra,
                                 systems::SystemsState& st);

std::vector<core::Entity> collect_spawn_post_monsters(const Loaded& g,
                                                      int screen);

void compose_surface_screen_static(
    const Loaded& g, int screen, presentation::FrameBuffer& out,
    presentation::LevelRenderAssets* out_ra = nullptr,
    const std::vector<presentation::LevelRenderAssets::TileDraw>* underlay =
        nullptr,
    bool frozen_full = false, bool peek_monsters = true);

void compose_surface_screen_wide_native(
    const Loaded& g, int screen, int margin,
    const presentation::FrameBuffer* backdrop, std::vector<std::uint8_t>& wide);

bool load_level(const std::filesystem::path& dir, Loaded& g,
                int internal_level, int start_screen = 0);

}  // namespace olduvai::presentation
