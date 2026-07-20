// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Full-state save: the complete game logic state, restored exactly (not
// reconstructed from an anchor — that was the broken checkpoint approach).
//
// Design (spec 2026-06-19-save-state-design.md, revised):
//  * SaveHeader — POD: PlayerState (trivially-copyable, memcpy'd whole) + every
//    gameplay scalar.  A new PlayerState field is auto-included; a SaveHeader
//    field is guarded by static_assert(is_trivially_copyable) + the round-trip
//    test.  Binary, magic + version tagged.
//  * Entities — NOT serialized whole (Entity carries derived spawn-time data:
//    walk_offsets, sprite indices, probe offsets).  Instead we snapshot only
//    the MUTABLE runtime fields; on restore the screen is re-bound (entities
//    re-spawn with correct derived data) and the snapshot is overlaid by index
//    (spawn order is deterministic; entities are deactivated, never erased).
//  * Multi-screen: the current live list + every visited screen's list (g.store)
//    are captured, so revisited screens keep their state.
//
// `capture`/`apply` over the full Loaded (g.store, bind) live in game_app.cpp;
// this header owns the schema, (de)serialization, and per-entity snap/overlay.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "core/types.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

struct SaveHeader {
    char magic[4] = {'O', 'L', 'S', 'V'};
    // 3 = the sole save format: full-state + a trailing layout tag
    // (sizeof(SaveHeader)) so a save written before/after any PlayerState/
    // SaveHeader field change is rejected instead of silently loading shifted
    // garbage (memcpy'd POD layout).  The pre-release v2 (untagged) read branch
    // and the dead v1 checkpoint format were removed for the first public
    // release — deserialize accepts version 3 only.
    std::int32_t version = 3;
    std::int32_t level = 1;            // DISPLAY level (run_game jumps here)
    std::uint32_t rng = 0;             // Rand_LCG16 32-bit state
    systems::PlayerState player{};     // whole (trivially copyable)
    // ── gameplay scalars (everything restore needs that isn't the player) ──
    std::int32_t current_screen = 0;   // encodes mode: >=100 = cave/secret
    std::int32_t screen_height = 199;
    std::int32_t frame_counter = 0, timer = 99, timer_counter = 0;
    std::int32_t cave_flag = 0, cave_index = -1;
    std::int32_t cave_return_screen = 0, cave_return_x = 0, cave_return_y = 0;
    std::int32_t secret_flag = 0, secret_index = -1;
    std::int32_t secret_return_screen = 0, secret_return_x = 0, secret_return_y = 0;
    std::int32_t cave_entrance_mask = 0;
    std::int32_t glider_active = 0, glider_x = 0, glider_y = -100;
    std::int32_t halo_flight_flag = 0;
    std::int64_t score = 0, next_life_score = 10000;
    std::int32_t food_count = 0, bonus_trigger = 0, l3a_phase_counter = 0;
    std::int32_t fireball_flag = 0, fireball_x = 0, fireball_y = 0;
    std::int32_t stone_state = 0, stone_x = 0, stone_y = 0;
    std::int32_t death_halo_active = 0, death_halo_x = 0, death_halo_y = 0;
    std::int32_t get_ready_counter = 0x11, screen_clear = 1, level_complete = 0;
    std::int32_t current_level = 1;    // internal id (sanity)
};
static_assert(std::is_trivially_copyable_v<SaveHeader>,
              "SaveHeader must stay POD so it memcpy-serializes whole");

// Mutable per-entity runtime fields (overlaid after re-spawn).  All int (bools
// widened) so the struct stays trivially copyable.
struct EntitySnapshot {
    std::int32_t obj_type = 0;   // index/type sanity check
    std::int32_t x = 0, y = 0, state = 0, sprite = 0;
    std::int32_t facing_left = 0, active = 1, visible = 1;
    std::int32_t frame_counter = 0, respawns = 0, direction = 0;
    std::int32_t state_counter = 0, ko_counter = 0;
    std::int32_t current_y = 0, dy = 0;
    std::int32_t throw_flag = 0, throw_x = 0, throw_y = 0, fireball_request = 0;
    std::int32_t counter = 0, bird_height_idx = 0, bird_anim = 0;
    std::int32_t draw_dy = 0, direction_flag = 0, spider_di = 0;
};
static_assert(std::is_trivially_copyable_v<EntitySnapshot>);

struct ScreenEntities {
    std::int32_t key = 0;   // g.store key
    std::vector<EntitySnapshot> entities;
};

struct SaveState {
    SaveHeader hdr;
    std::vector<EntitySnapshot> entities;   // current (live) screen
    std::vector<ScreenEntities> store;      // every visited screen's list
    std::int32_t bound_key = -1;
};

// Per-entity snapshot / overlay (the runtime fields only).
EntitySnapshot snapshot_entity(const core::Entity& e);
void overlay_entity(const EntitySnapshot& s, core::Entity& e);

// Header capture / apply (scalars + player; not entities, not RNG seed).
SaveHeader capture_header(const systems::SystemsState& st, int display_level);
void apply_header(const SaveHeader& h, systems::SystemsState& st);

// Binary (de)serialization + file I/O.
std::vector<std::uint8_t> serialize(const SaveState& s);
std::optional<SaveState> deserialize(const std::vector<std::uint8_t>& bytes);
bool save_to_file(const SaveState& s, const std::string& path);
std::optional<SaveState> load_from_file(const std::string& path);

}  // namespace olduvai::presentation
