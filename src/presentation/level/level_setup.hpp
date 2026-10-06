// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Surface-level asset loading, screen binding and static composition.
//
// Only the functions run_platform_level/run_game actually call are declared
// here; the rest (store_key, bind_store, load_level_impl) stay file-local in
// level_setup.cpp.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "presentation/audio/audio.hpp"        // SdlAudio
#include "presentation/level/level_state.hpp"  // Loaded, LevelConfig (+ render types)

namespace olduvai::presentation {

// Read the runtime gameplay tables (cave widths, secret scores) and the
// AdLib SFX voice patches from the user's executable bytes and install
// them (core::install_game_tables + install_adlib_sfx_voices).  Idempotent;
// called at app start and again on every level load.
void install_exe_game_data(const std::vector<std::uint8_t>& exe);

void load_sfx_bank(SdlAudio& audio,
                   const std::function<const std::vector<std::uint8_t>*(
                       const std::string&)>& entry);

void refresh_secret_tiles(Loaded& g, bool draw_scatter);

// Warm the HD sprite cache while "Please Wait" shows.  Lazily, the upscales
// hit the first frame that draws each sprite (1138 ms for 56 sprites entering
// the L1 secret room on a Cortex-A53).
void warm_level_sprites(Loaded& g, int hd_scale, const std::string& profile);

void bind_screen(Loaded& g, int screen);

// Enhanced icy-glider (internal level 5): flatten the decorative water (sprite
// 7, no collision) to the flight-start (screen 9) sea level across the flight
// screens so it reads as one continuous body during the glider.  Visual-only;
// a no-op unless `enhanced` and `internal == 5`.  bind_screen applies this per
// screen; call once at level entry for the already-bound entry screen.
void setup_enhanced_glider_water(Loaded& g, bool enhanced, int internal);

void build_surface_screen_assets(const Loaded& g, int screen,
                                 presentation::LevelRenderAssets& ra,
                                 systems::SystemsState& st);

// Enhanced, a screen entered for the first time: the monsters its first
// update will show appear from the transition's first frame, in their first
// emerge frame.  The EXE draws the new page before that update, so they pop
// in after the pan (Classic keeps that).  The rule is the update's own
// (systems::appear_if_player_level): one hidden in a cave until the player
// is level with it stays hidden.  While it lives, those monsters of
// g.state.entities read as appeared; it restores them exactly, so no
// simulation sees it.
class SpawnPostPreview {
  public:
    explicit SpawnPostPreview(Loaded& g);
    ~SpawnPostPreview();
    SpawnPostPreview(const SpawnPostPreview&) = delete;
    SpawnPostPreview& operator=(const SpawnPostPreview&) = delete;

  private:
    Loaded& g_;
    std::vector<std::pair<std::size_t, core::Entity>> saved_;
};

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
