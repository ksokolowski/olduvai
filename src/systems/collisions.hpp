// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Player-entity collision detection — the collision portion of the
// per-frame entity dispatcher, split per type.  // FUN_2A04_0003
//
// Three phases: club-hit checks (second swing frame only), body-overlap
// checks (skipping entities already club-hit this frame), and the chimp
// projectile pass.  Per-monster club reach and damage bounds come from the
// 48-byte data tables (bound at spawn).

#pragma once

#include <vector>

#include "core/types.hpp"

namespace olduvai::systems {

struct CollisionContext {
    int player_x = 0;
    int player_y = 0;
    int player_w = 32;            // default player hitbox
    int player_h = 30;
    bool attacking = false;
    int club_flag = 0;
    bool facing_left = false;
    bool key_up = false;
    bool key_down = false;
    bool climbing = false;
    int cave_exit_x = -1;
    int axe_flag = 0;             // axe-powered (DS:0x97f8) — extra hit point
    int level = 1;
    int gravity_flag = 0;
};

core::CollisionResult check_player_collisions(
    std::vector<core::Entity>& entities, const CollisionContext& ctx);

}  // namespace olduvai::systems
