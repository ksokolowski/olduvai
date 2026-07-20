// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Per-frame entity update handlers (batch 1).
//
// The shared monster state machine          // FUN_27f7_093d
//   RESET(0) → SPAWN(1) → HEADING(3) ⇄ RUNNING_AWAY(4) → KO(5) → DEAD(6)
// and the per-type dispatcher               // FUN_2A04_0003
// This batch: shared monster, fish, platform, egg, rock, no-op types.
// Remaining handlers (bird, chimp, spider, bat, snake, projectile, bonus,
// pterodactyl, jumping fish, L7 variants, animated food, breakable rock,
// fire/peak) land in batch 2.

#pragma once

#include <vector>

#include "core/collision_bitmap.hpp"
#include "core/types.hpp"

namespace olduvai::systems {

constexpr int kSprFishUp = 83;
constexpr int kSprFishDown = 84;
constexpr int kSprEgg = 125;
constexpr int kSprRock = 143;

struct UpdateEntitiesResult {
    int l3a_phase_counter = 0;
    // DS:0x9860 semantics: true only if every monster slot on the screen is
    // permanently dead.  Read by the L3 screen-17 food gate and the L7
    // lava-spring warp.
    bool screen_clear_of_monsters = true;
};

UpdateEntitiesResult update_entities(
    std::vector<core::Entity>& entities, int player_x, int player_y,
    int frame, const core::CollisionBitmap* collision,
    int l3a_phase_counter = 0, bool kill_all = false,
    bool axe_powered = false, bool fireball_active = false);

// Recompute e.sprite from current state for shared-state-machine
// monsters.  Call whenever the live entity list is (re)bound to a
// screen: the first composed frame after a bind runs before any entity
// update, so stored sprites (init placeholders on first entry, stale
// frames on re-entry) would otherwise leak into it — and into every
// frame of the screen-transition slide.
void refresh_entity_sprites_on_screen_bind(
    std::vector<core::Entity>& entities, int l3a_phase_counter);

// Individual handlers (exposed for scenario tests).
void update_monster(core::Entity& e, int px, int py, int frame,
                    const core::CollisionBitmap* collision,
                    int l3a_phase_counter, bool axe_powered,
                    bool fireball_active);
void update_fish(core::Entity& e);
void update_platform(core::Entity& e);
void update_egg(core::Entity& e);
void update_rock(core::Entity& e);
// Batch 2:
void update_monster_l7_a(core::Entity& e, int px, int py, int frame,
                         const core::CollisionBitmap* collision,
                         int l3a_phase_counter, bool axe_powered);
void update_bird(core::Entity& e, int px);
void update_chimp(core::Entity& e);            // CHIMP + CHIMP_L5 (snowman)
void update_chimp_l7(core::Entity& e);         // fish-arc with x scatter
void update_pterodactyl_l7(core::Entity& e, int frame);
void update_jumping_fish_l5(core::Entity& e);
void update_fire(core::Entity& e, int frame);
void update_cave_spider(core::Entity& e);
void update_cave_bat(core::Entity& e, int frame);
void update_snake(core::Entity& e, int frame);
void update_animated_food_l3(core::Entity& e, int frame);
void update_bonus(core::Entity& e, int px, int frame);
void update_breakable_rock_l3(core::Entity& e);
void update_projectile_l3(core::Entity& e, int px);

}  // namespace olduvai::systems
