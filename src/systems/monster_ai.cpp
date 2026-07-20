// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/monster_ai.hpp"

#include <cstdlib>

#include "core/rng.hpp"

namespace olduvai::systems {

namespace {
// Bird wing animation table (DS:0x80a8).
constexpr int kBirdSprTable[8] = {1, 0, 1, 2, 1, 0, 1, 2};
// Animated food L3 sprite table (DS:0x80b8, 0-based).
constexpr int kAnimFoodL3Sprites[4] = {94, 84, 83, 84};
// Bonus icon sprites by type (DS:0x8088, 0-based).
constexpr int kBonusSprites[6] = {90, 81, 89, 130, 131, 139};
constexpr int kSprBonusSmokeBase = 85;
constexpr int kSprBonusGhost = 88;
constexpr int kSprBirdKo = 43;
constexpr int kSprChimp = 92;
constexpr int kSprSpider = 118;
constexpr int kSprBat = 121;
constexpr int kSprSnakeBody = 149;
constexpr int kSprSnakeFlat = 150;
constexpr int kSprSnakeMid = 151;
constexpr int kSprSnakeExtended = 152;
constexpr int kSprMonsterL7ADormant = 50;
constexpr int kSprRockL3 = 116;
constexpr int kSprJFishRiseLeft = 55, kSprJFishFallLeft = 56;
constexpr int kSprJFishRiseRight = 57, kSprJFishFallRight = 58;
}  // namespace

using core::CollisionBitmap;
using core::Entity;
using core::MonsterState;
using core::ObjType;

namespace {

bool is_shared_monster(ObjType t) {
    switch (t) {
        case ObjType::RedDino:
        case ObjType::YellowFuzz:
        case ObjType::BrownBear:
        case ObjType::GreenDino:
        case ObjType::MonsterL3A:
        case ObjType::MonsterL3B:
        case ObjType::MonsterL5A:
        case ObjType::MonsterL5B:
        case ObjType::MonsterL7A:
        case ObjType::MonsterL7B:
            return true;
        default:
            return false;
    }
}

int state_of(const Entity& e) { return e.state; }

void set_heading_sprite(Entity& e, int l3a_phase_counter) {
    const bool use_alt = (e.alt_spr_num >= 0 && l3a_phase_counter <= 16);
    const int spr_base = use_alt ? e.alt_spr_num : e.spr_num;
    const auto& offsets = use_alt ? e.alt_walk_offsets : e.walk_offsets;
    if (!offsets.empty()) {
        const int idx = e.state_counter % static_cast<int>(offsets.size());
        e.sprite = spr_base + offsets[static_cast<std::size_t>(idx)];
    } else {
        e.sprite = spr_base + (e.state_counter & 1);
    }
}

void set_away_sprite(Entity& e, int l3a_phase_counter) {
    const bool use_alt = (e.alt_away_spr >= 0 && l3a_phase_counter <= 16);
    e.sprite = use_alt ? e.alt_away_spr : e.away_spr;
}

}  // namespace

void update_monster(Entity& e, int px, int py, int frame,
                    const CollisionBitmap* collision,
                    int l3a_phase_counter, bool axe_powered,
                    bool fireball_active) {
    const int di = e.probe_di;
    const int si = e.probe_si;

    // ── DEAD → respawn if lives remain ──
    if (state_of(e) >= static_cast<int>(MonsterState::Dead)) {
        e.fireball_request = 0;
        if (e.respawns > 0) {
            e.state = static_cast<int>(MonsterState::Reset);
            --e.respawns;
            e.x = e.init_x;
            e.y = e.init_y;
            e.direction = 0;
            e.state_counter = 0;
            e.ko_counter = 0;
            e.visible = false;
            e.facing_left = false;
        } else {
            e.active = false;
        }
        return;
    }

    // ── KO: counter counts DOWN from 40 ──
    if (state_of(e) == static_cast<int>(MonsterState::Ko)) {
        e.fireball_request = 0;
        if (e.ko_counter <= 0) e.ko_counter = 40;
        e.sprite = e.ko_spr + (e.ko_counter & 1);  // +0x10EA
        // Decrement gated on tick parity (even frames only) — half rate.
        if ((frame & 1) == 0) --e.ko_counter;      // +0x117E..0x1188
        if (e.ko_counter <= 0) {                   // expired → recover
            e.state = static_cast<int>(MonsterState::HeadingPlayer);
            e.state_counter = 0;
        }
        return;
    }

    // ── RESET: activate when the player is near the monster's Y ──
    if (state_of(e) == static_cast<int>(MonsterState::Reset)) {
        if (px <= 2) return;
        if (py + 30 <= e.y || py - 15 >= e.y) return;
        e.state = static_cast<int>(MonsterState::Spawn);
        e.state_counter = 0;
        e.ko_counter = 0;
        e.direction = (e.x + 20 >= px) ? 1 : 0;
        e.visible = true;
        e.sprite = e.init_spr;
        return;
    }

    // ── SPAWN: 2-frame spawn animation ──
    if (state_of(e) == static_cast<int>(MonsterState::Spawn)) {
        e.sprite = e.init_spr + e.state_counter;
        ++e.state_counter;
        if (e.state_counter >= 2) {
            e.state = static_cast<int>(MonsterState::HeadingPlayer);
            e.state_counter = 0;
        }
        return;
    }

    // ── HEADING_PLAYER ──
    if (state_of(e) == static_cast<int>(MonsterState::HeadingPlayer)) {
        // Tick-parity gate: odd frame → sprite/facing only.  // +0x0bd7
        if (frame & 1) {
            set_heading_sprite(e, l3a_phase_counter);
            e.facing_left = (e.direction != 0);
            return;
        }

        if (e.direction != 0) e.x -= 4;
        else e.x += 4;

        bool has_ground = true;
        if (collision != nullptr) {
            if (collision->test(e.x + di, e.y + si) ||
                collision->test(e.x, e.y + si)) {
                has_ground = false;
            }
        }

        if (e.x >= 20 && e.x <= 275 && has_ground) {
            if (e.state_counter == 0) {
                // 3-in-20 random direction flip.  // +0x0D0A
                const int rng = (frame * 7 + e.x * 13 + e.y) % 20;
                if (rng == 2 || rng == 12 || rng == 16) e.direction ^= 1;
                // Player tracking within Y window +10/−5.  // +0x0D37
                if (py + 10 > e.y && py - 5 < e.y) {
                    e.direction = (e.x + 20 >= px) ? 1 : 0;
                }
            }
            ++e.state_counter;
        } else {
            // Edge/obstacle: clamp, reverse, walk back to solid ground.
            if (e.x < 20) e.x = 20;
            else if (e.x > 275) e.x = 275;
            e.direction ^= 1;
            if (collision != nullptr) {
                for (int n = 0; n < 320; ++n) {
                    if (!collision->test(e.x + di, e.y + si) &&
                        !collision->test(e.x, e.y + si)) {
                        break;
                    }
                    if (e.direction != 0) --e.x;
                    else ++e.x;
                }
            }
            ++e.state_counter;
        }

        e.state_counter &= 7;
        set_heading_sprite(e, l3a_phase_counter);
        e.facing_left = (e.direction != 0);

        // Fire-monster fireball spawn + attack sprite.  // +0x0d95..0x0e1c
        // LOS direction uses the ACTUAL player position (intentional
        // divergence from the stored-direction X-check; owner gameplay
        // memory — no visible backward fire in the original).
        if (e.obj_type == ObjType::YellowFuzz && !fireball_active) {
            e.fireball_request = 0;
            if (std::abs(py - e.y) <= 5 && px != e.x) {
                const int fire_dir = (px < e.x) ? 1 : 0;
                e.direction = fire_dir;
                e.fireball_request = fire_dir + 1;
                e.sprite = e.spr_num + 2;  // attack sprite
            }
        }
        return;
    }

    // ── RUNNING_AWAY ──
    if (state_of(e) == static_cast<int>(MonsterState::RunningAway)) {
        // Tick-parity gate (odd frame): counter/sprite only.  // +0x0f5e
        if (frame & 1) {
            set_away_sprite(e, l3a_phase_counter);
            e.facing_left = (e.direction != 0);
            ++e.state_counter;
            if (e.state_counter >= 2) {
                e.state = static_cast<int>(MonsterState::HeadingPlayer);
                e.direction ^= e.direction_flag;
                e.state_counter = 0;
                ++e.ko_counter;
                // hits_to_ko reached OR axe one-shot → KO.  // +0x10c9
                if (e.ko_counter >= e.hits_to_ko || axe_powered) {
                    e.state = static_cast<int>(MonsterState::Ko);
                    e.ko_counter = 40;
                }
            }
            return;
        }

        // Direction sense reversed from HEADING.
        if (e.direction != 0) e.x += 8;
        else e.x -= 8;

        bool has_ground = true;
        if (collision != nullptr) {
            if (collision->test(e.x + di, e.y + si) ||
                collision->test(e.x, e.y + si)) {
                has_ground = false;
            }
        }
        if (!(e.x >= 20 && e.x <= 275 && has_ground)) {
            if (e.x < 20) e.x = 20;
            else if (e.x > 275) e.x = 275;
            if (collision != nullptr) {
                const int step = (e.direction == 0) ? 1 : -1;
                for (int n = 0; n < 320; ++n) {
                    if (!collision->test(e.x + di, e.y + si) &&
                        !collision->test(e.x, e.y + si)) {
                        break;
                    }
                    e.x += step;
                }
            }
        }

        set_away_sprite(e, l3a_phase_counter);
        e.facing_left = (e.direction != 0);

        ++e.state_counter;
        if (e.state_counter >= 2) {                 // +0x10A0
            e.state = static_cast<int>(MonsterState::HeadingPlayer);
            e.direction ^= e.direction_flag;
            e.state_counter = 0;
            ++e.ko_counter;
            if (e.ko_counter >= e.hits_to_ko || axe_powered) {
                e.state = static_cast<int>(MonsterState::Ko);
                e.ko_counter = 40;
            }
        }
        return;
    }
}

void update_fish(Entity& e) {
    // TYPE 0x09 jump arc; dy stores the velocity.  // dispatcher +0x075c
    if (e.state == 0) {  // ascending
        e.sprite = kSprFishUp;
        e.y -= e.dy;
        --e.dy;
        if (e.y < core::CollisionBitmap::kHeight) e.visible = true;
        if (e.dy == 0) e.state = 1;
    } else {             // descending
        e.sprite = kSprFishDown;
        e.y += e.dy;
        ++e.dy;
        if (e.dy == 20) {   // +0x07cf..0x07d7: reset; x and dy untouched
            e.state = 0;
            e.y = 220;
            e.visible = false;
        }
    }
}

void update_platform(Entity& e) {
    // TYPE 0x10.  Bounds use the PRE-move position (snapshot into e.y);
    // direction is a 0/1 flag, not ±1.  // dispatcher +0x0bea..0x0c9d
    e.y = e.current_y;
    if (e.direction == 0) {
        e.current_y -= e.speed;
        if (e.current_y < e.y_top) e.direction = 1;
    } else {
        e.current_y += e.speed;
        if (e.current_y > e.y_bottom) e.direction = 0;
    }
}

void update_egg(Entity& e) {
    // counter = hit points (max 2); sprite 125 whole / 126 cracked.
    if (e.counter == 0) {
        e.active = false;
        e.visible = false;
        return;
    }
    e.sprite = kSprEgg + (2 - e.counter);
    e.visible = true;
}

void update_rock(Entity& e) {
    // counter = hit points (max 3); sprites 143/144/145.
    if (e.counter == 0) {
        e.active = false;
        e.visible = false;
        return;
    }
    e.sprite = kSprRock + (3 - e.counter);
    e.visible = true;
}

void update_monster_l7_a(Entity& e, int px, int py, int frame,
                         const CollisionBitmap* collision,
                         int l3a_phase_counter, bool axe_powered) {
    // TYPE 0x26: dormant until the player comes within 70 px of init_x;
    // afterwards the shared state machine runs.  // dispatcher +0x1ca5
    const bool out_of_range = std::abs(px - e.init_x) > 0x46;
    if (e.state != 0 || !out_of_range) {
        update_monster(e, px, py, frame, collision, l3a_phase_counter,
                       axe_powered, /*fireball_active=*/false);
    }
    if (e.state == 0) {  // dormant: static lava-bubble at the init position
        e.x = e.init_x;
        e.y = e.init_y;
        e.sprite = kSprMonsterL7ADormant;
        e.visible = true;
    }
}

void update_bird(Entity& e, int px) {
    // TYPE 0x0f: waiting (invisible) → flying left 8 px/frame with the wing
    // table + height cycling → KO knockback right 30 px/frame for 6 frames.
    if (e.state == 0) {
        if (px < 220) {
            e.state = 1;
            e.x = e.bird_spawn_x;   // EXE 355; widescreen raises it (render)
            e.bird_height_idx = (e.bird_height_idx + 1) & 3;
            e.visible = true;
        } else {
            e.visible = false;
        }
        e.y = e.bird_heights[static_cast<std::size_t>(e.bird_height_idx)];
        e.sprite = e.spr_num + kBirdSprTable[e.bird_anim & 7];
        return;
    }
    e.y = e.bird_heights[static_cast<std::size_t>(e.bird_height_idx)];
    if (e.state == 1) {
        e.x -= 8;
        e.sprite = e.spr_num + kBirdSprTable[e.bird_anim & 7];
        e.bird_anim = (e.bird_anim + 1) & 7;
        if (e.x < e.off_screen_left) {   // EXE -50; widescreen lowers it (render)
            e.state = 0;
            e.visible = false;
        }
        return;
    }
    if (e.state == 2) {  // KO knockback
        e.x += 30;
        ++e.bird_anim;
        if (e.bird_anim >= 6) e.state = 1;
        e.sprite = kSprBirdKo;
    }
}

void update_chimp(Entity& e) {
    // CHIMP (3-state) and CHIMP_L5 snowman (6-state windup + KO slide).
    // Projectile: constant −12 px/frame, despawn at x < −70.
    e.draw_dy = 0;

    if (e.obj_type == ObjType::ChimpL5) {
        if (e.state >= 8) {
            e.active = false;
            e.visible = false;
        } else if (e.state >= 7) {  // KO exit slide
            e.sprite = kSprChimp + 2;
            e.x += 10;
            if (e.x > 320) e.state = 8;
        } else if (e.state == 0) {
            e.sprite = kSprChimp;
            if (e.throw_flag == 0) e.state = 1;
        } else {  // windup states 1..5, single windup sprite, +5 y offset
            e.sprite = kSprChimp + 1;
            e.draw_dy = 5;
            int new_state = e.state + 1;
            if (new_state == 2) {        // spawn projectile
                e.throw_flag = 1;
                e.throw_x = e.x - 12;
                e.throw_y = e.y + 18;
            }
            if (new_state == 6) new_state = 0;  // 4 recovery frames built in
            e.state = new_state;
        }
    } else {
        if (e.state >= 4) {
            e.active = false;
            e.visible = false;
        } else if (e.state == 0) {
            e.sprite = kSprChimp + 1;
            if (e.throw_flag == 0) e.state = 1;
        } else if (e.state < 3) {
            e.sprite = kSprChimp + (e.state - 1);
            ++e.state;
            if (e.state == 3) {
                e.throw_flag = 1;
                e.throw_x = e.x - 12;
                e.throw_y = e.y + 18;
                e.state = 0;
            }
        } else {  // KO exit slide
            e.sprite = kSprChimp + 2;
            e.x += 10;
            if (e.x > 320) e.state = 4;
        }
    }

    if (e.throw_flag != 0) {
        e.throw_x -= 12;
        if (e.throw_x < -70) e.throw_flag = 0;
    }
}

void update_chimp_l7(Entity& e) {
    // TYPE 0x24: structurally a fish arc; x scatters into the anchor range
    // on reset via the shared LCG.  // dispatcher +0x1b3b..0x1be0
    if (e.state == 0) {
        e.sprite = kSprFishUp;
        e.y -= e.dy;
        --e.dy;
        if (e.dy == 0) e.state = 1;
    } else {
        e.sprite = kSprFishDown;
        e.y += e.dy;
        ++e.dy;
        if (e.dy == 20) {
            e.state = 0;
            e.y = 220;
            const int rng = core::global_rng().next();
            const int width = e.anchor_a - e.anchor_b;
            e.x = (width > 0) ? rng % width + e.anchor_b : rng % 0xD2;
        }
    }
}

void update_pterodactyl_l7(Entity& e, int frame) {
    // TYPE 0x23: cosmetic dual-sprite lava bubbles.  Sub-counters advance on
    // even frames; phase wrap scatters x via the shared LCG.  Logical phase
    // ranges map onto the 3-frame physical sprite groups.
    const bool even_frame = (frame & 1) != 0;

    e.sprite = (e.body_phase - 0x38) % 3 + 55;
    if (even_frame) ++e.body_subcounter;
    if (e.body_subcounter >= 3) {
        e.body_subcounter = 0;
        ++e.body_phase;
        if (e.body_phase > 0x3D) {
            e.body_phase = 0x38;
            const int rng = core::global_rng().next();
            const int width = e.anchor_a - e.anchor_b;
            if (width > 0) e.x = rng % width + e.anchor_b;
        }
    }

    e.body_sprite = (e.wing_phase - 0x4D) % 3 + 76;
    e.body_x = e.wing_x;
    e.body_y = 165;
    if (even_frame) ++e.wing_subcounter;
    if (e.wing_subcounter >= 3) {
        e.wing_subcounter = 0;
        ++e.wing_phase;
        if (e.wing_phase > 0x51) {
            e.wing_phase = 0x4D;
            const int rng = core::global_rng().next();
            const int width = e.anchor_a - e.anchor_b;
            if (width > 0) e.wing_x = rng % width + e.anchor_b;
        }
    }
}

void update_jumping_fish_l5(Entity& e) {
    // TYPE 0x1f: direction-aware drift ±3 px; reset x = LCG % 0xD2.
    if (e.state == 0) {
        if (e.direction == 0) {
            e.x -= 3;
            e.sprite = kSprJFishRiseLeft;
        } else {
            e.x += 3;
            e.sprite = kSprJFishRiseRight;
        }
        e.y -= e.dy;
        --e.dy;
        if (e.dy == 0) e.state = 1;
    } else {
        if (e.direction == 0) {
            e.x -= 3;
            e.sprite = kSprJFishFallLeft;
        } else {
            e.x += 3;
            e.sprite = kSprJFishFallRight;
        }
        e.y += e.dy;
        ++e.dy;
        if (e.dy == 20) {
            e.state = 0;
            e.y = 220;
            e.x = core::global_rng().next() % 0xD2;
        }
    }
}

void update_fire(Entity& e, int frame) {
    // TYPE 0x13: sprite-select bit toggles on even frames.
    ++e.frame_counter;
    if (!(frame & 1)) e.counter ^= 1;
    e.sprite = e.spr_num + e.counter;
}

void update_cave_spider(Entity& e) {
    // TYPE 0x16: counter cycles 0..17; only the draw offset + hitbox extent
    // change (the spider never moves and cannot be killed).
    if (e.counter >= 17) e.counter = 0;
    else ++e.counter;
    const int c = e.counter;
    if (c >= 2 && c <= 7) {
        if (c == 2 || c == 6 || c == 7) {
            e.sprite = kSprSpider + 1;
            e.draw_dy = 10;
            e.spider_di = 45;
        } else {  // 3..5 fully extended
            e.sprite = kSprSpider + 2;
            e.draw_dy = 16;
            e.spider_di = 64;
        }
    } else {
        e.sprite = kSprSpider;
        e.draw_dy = 0;
        e.spider_di = 0;
    }
}

void update_cave_bat(Entity& e, int frame) {
    // TYPE 0x17: random flutter inside the cave bounds on even frames; wing
    // toggle via mask bit 0; mask bit 1 = dead.  Flutter randomness routes
    // through the shared LCG with the SAME draws in the SAME order as the
    // reference engine (rand_lcg16()%7-3, %11-5, even-
    // frame gated).  This IS part of the shared-sequence parity contract:
    // changing the draw pattern here forks every later RNG event.
    if (e.mask & 2) return;
    const int cave_left = e.y_top;
    const int cave_right = e.y_bottom;
    if (!(frame & 1)) {
        e.y += static_cast<int>(core::global_rng().next() % 7) - 3;  // ±3
        e.x += static_cast<int>(core::global_rng().next() % 11) - 5; // ±5
    }
    if (e.x < cave_left) e.x = cave_left;
    if (e.x > cave_right - 30) e.x = cave_right - 30;
    if (e.y > 140) e.y = 130;
    if (!(frame & 1)) e.mask ^= 1;
    e.sprite = kSprBat + (e.mask & 1);
}

void update_snake(Entity& e, int frame) {
    // TYPE 0x1d: counter advances on odd frames, wraps 40→0 the same frame;
    // body always at (x, y), head at (x+16, y+20) with phase sprites.
    if (frame & 1) ++e.counter;
    if (e.counter == 40) e.counter = 0;
    const int c = e.counter;
    e.body_sprite = kSprSnakeBody;
    e.body_x = e.x;
    e.body_y = e.y;
    e.head_x = e.x + 16;
    e.head_y = e.y + 20;
    if (c == 11 || c == 20) e.sprite = kSprSnakeMid;
    else if (c >= 12 && c <= 19) e.sprite = kSprSnakeExtended;
    else e.sprite = kSprSnakeFlat;
}

void update_animated_food_l3(Entity& e, int frame) {
    // TYPE 0x18: hazard plant; counter on odd frames; spr_num phase walk
    // 0→1→2→3→0 driven by thresholds 15/30/45 (45 resets counter to 14).
    if (frame & 1) ++e.counter;
    if (e.spr_num == 3) e.spr_num = 0;
    else if (e.spr_num == 1) e.spr_num = 2;
    if (e.counter == 15) e.spr_num = 1;
    else if (e.counter == 30) e.spr_num = 3;
    else if (e.counter == 45) e.counter = 14;
    e.sprite = kAnimFoodL3Sprites[e.spr_num & 3];
}

void update_bonus(Entity& e, int px, int frame) {
    (void)frame;
    // TYPE 0x05 ancestor ghost.  Rising-bonus mode (mask bit 7) follows the
    // player's x while the icon arcs (-20 → +20, step +4); on completion
    // bonus_pending signals Bonus_Activate to the game loop.
    if (e.mask & 0x80) {
        const int bonus_type = e.mask & 0x7F;
        if (bonus_type >= 0 && bonus_type < 6) {
            e.sprite = kBonusSprites[bonus_type];
        }
        e.x = px;
        e.bonus_rise_dy += 4;
        e.bonus_rise_y += e.bonus_rise_dy;
        e.y = e.bonus_rise_y;
        e.visible = true;
        if (e.bonus_rise_dy == 20) {
            e.bonus_pending = true;
            e.mask = bonus_type;
        }
        return;
    }

    const int count = e.counter;
    if (count >= 70) {
        e.visible = false;
        return;
    }
    ++e.counter;
    if (count < 16) {
        e.visible = false;
        return;
    }
    e.visible = true;
    if (count <= 20) {  // smoke appear 85,85,86,86,87
        e.sprite = kSprBonusSmokeBase + (count - 16) / 2;
        return;
    }
    if (count == 21) {  // pick the random bonus type (0..5)
        e.mask = static_cast<int>(core::global_rng().next() % 6);
    }
    if (count < 65) {   // ghost visible, ±1 px wobble
        e.sprite = kSprBonusGhost;
        switch (count & 3) {
            case 0: ++e.x; break;
            case 1: ++e.y; break;
            case 2: --e.x; break;
            default: --e.y; break;
        }
        return;
    }
    // smoke disappear 87,87,86,86,85
    e.sprite = kSprBonusSmokeBase + 2 - (count - 65) / 2;
}

void update_breakable_rock_l3(Entity& e) {
    // 2 HP: sprite 116 intact, 125 cracked; 0 HP deactivates.
    if (e.counter == 0) {
        e.active = false;
        e.visible = false;
        return;
    }
    e.sprite = (e.counter >= 2) ? kSprRockL3 : kSprRockL3 + 9;
    e.visible = true;
}

void update_projectile_l3(Entity& e, int px) {
    // TYPE 0x1e launcher + falling projectile.  States: 0 reset/re-aim,
    // 1 fire left (−6/f), 2 idle, 3 fire right (+6/f); gravity 7 px/frame.
    const int si = e.init_x, di = e.init_y;

    if (e.state == 0) {
        e.proj_x = si + 5;
        e.proj_y = di + 5;
        e.state = 2;
        if (si - 30 > px) {
            e.proj_x = si + 4;
            e.proj_y = di + 5;
            e.state = 1;
        }
        if (si + 30 < px) {
            e.proj_x = si + 8;
            e.proj_y = di + 5;
            e.state = 3;
        }
    }

    int proj_spr = 153;
    e.proj_y += 7;
    if (e.state == 1) {
        proj_spr = 147;
        e.proj_x -= 6;
        if (e.proj_x < -10) e.state = 0;
    }
    if (e.state == 3) {
        proj_spr = 148;
        e.proj_x += 6;
        if (e.proj_x > 320) e.state = 0;
    }
    e.launcher_spr = (e.proj_y > di + 30) ? 145 : 146;
    if (e.proj_y > 200) e.state = 0;
    e.sprite = proj_spr;
    e.x = e.proj_x;
    e.y = e.proj_y;
}

void refresh_entity_sprites_on_screen_bind(std::vector<Entity>& entities,
                                           int l3a_phase_counter) {
    // The EXE dispatcher (FUN_2A04_0003 + FUN_27f7_093d) re-COMPUTES the
    // sprite from state every frame; the engine STORES it in the entity.
    // The first frame after a screen bind composes BEFORE any entity
    // update, so without this the stored value leaks into that frame:
    // init_spr placeholders on first entry, last-visit stale frames on
    // re-entry — and the slide transition displays that frame for its
    // whole duration.  Mirrors the reference fix (commit d4c8f39,
    // finding wrong_sprite_one_frame_on_screen_entry.md).
    for (Entity& e : entities) {
        if (!e.active || !e.visible) continue;
        // Only the shared state machine drives sprite from state in a
        // way that disagrees with the stored value.  MonsterL7A runs
        // its own gated handler — excluded in the reference too.
        if (!is_shared_monster(e.obj_type) ||
            e.obj_type == ObjType::MonsterL7A) {
            continue;
        }
        switch (state_of(e)) {
            case static_cast<int>(MonsterState::Ko):
                e.sprite = e.ko_spr + (e.ko_counter & 1);   // +0x10EA
                break;
            case static_cast<int>(MonsterState::Spawn):
                e.sprite = e.init_spr + e.state_counter;
                break;
            case static_cast<int>(MonsterState::HeadingPlayer):
                set_heading_sprite(e, l3a_phase_counter);
                break;
            case static_cast<int>(MonsterState::RunningAway):
                set_away_sprite(e, l3a_phase_counter);
                break;
            default:   // RESET / DEAD: invisible — leave alone
                break;
        }
    }
}

UpdateEntitiesResult update_entities(std::vector<Entity>& entities,
                                     int player_x, int player_y, int frame,
                                     const CollisionBitmap* collision,
                                     int l3a_phase_counter, bool kill_all,
                                     bool axe_powered, bool fireball_active) {
    UpdateEntitiesResult out;
    // L3A global phase: increments on even frames, wraps at 32.  // +0x1291
    if ((frame & 1) == 0) l3a_phase_counter = (l3a_phase_counter + 1) & 0x1F;
    out.l3a_phase_counter = l3a_phase_counter;

    // Bomb power-up pre-dispatch: every live monster → permanent KO (state 5,
    // ko_counter 8000 unless a shorter KO is already running); respawn count
    // cleared unconditionally.  KO (not DEAD) so body-collect still yields
    // food/score.  // FUN_27f7_093d +0x0a0f..0x0a33
    if (kill_all) {
        for (Entity& e : entities) {
            if (!e.active || !is_shared_monster(e.obj_type)) continue;
            e.respawns = 0;
            if (e.state != static_cast<int>(MonsterState::Dead)) {
                e.state = static_cast<int>(MonsterState::Ko);
                if (e.ko_counter < 100) e.ko_counter = 8000;
                e.visible = true;  // engine visibility flag (see reference)
            }
        }
    }

    for (Entity& e : entities) {
        if (!e.active) continue;
        switch (e.obj_type) {
            case ObjType::RedDino:
            case ObjType::YellowFuzz:
            case ObjType::BrownBear:
            case ObjType::GreenDino:
            case ObjType::MonsterL3A:
            case ObjType::MonsterL3B:
            case ObjType::MonsterL5A:
            case ObjType::MonsterL5B:
            case ObjType::MonsterL7B:
                // DS:0x9860: any slot in a live state (or DEAD with respawns
                // pending) clears the screen-clear latch.  // +0x18a3/0x18b3
                if (e.state < static_cast<int>(MonsterState::Dead) ||
                    e.respawns > 0) {
                    out.screen_clear_of_monsters = false;
                }
                update_monster(e, player_x, player_y, frame, collision,
                               l3a_phase_counter, axe_powered,
                               fireball_active);
                break;
            case ObjType::MonsterL7A:
                if (e.state < static_cast<int>(MonsterState::Dead) ||
                    e.respawns > 0) {
                    out.screen_clear_of_monsters = false;
                }
                update_monster_l7_a(e, player_x, player_y, frame, collision,
                                    l3a_phase_counter, axe_powered);
                break;
            case ObjType::Fish:
                update_fish(e);
                break;
            case ObjType::Platform:
                update_platform(e);
                break;
            case ObjType::Egg:
                update_egg(e);
                break;
            case ObjType::Rock:
                update_rock(e);
                break;
            case ObjType::Bird:
                update_bird(e, player_x);
                break;
            case ObjType::Chimp:
            case ObjType::ChimpL5:
                update_chimp(e);
                break;
            case ObjType::ChimpL7:
                update_chimp_l7(e);
                break;
            case ObjType::PteriyakiL7:
                update_pterodactyl_l7(e, frame);
                break;
            case ObjType::JumpingFishL5:
                update_jumping_fish_l5(e);
                break;
            case ObjType::Fire:
                update_fire(e, frame);
                break;
            case ObjType::CaveSpider:
                update_cave_spider(e);
                break;
            case ObjType::CaveBat:
                update_cave_bat(e, frame);
                break;
            case ObjType::SnakeL3:
                update_snake(e, frame);
                break;
            case ObjType::AnimatedFoodL3:
                update_animated_food_l3(e, frame);
                break;
            case ObjType::AncestorGhost:
                update_bonus(e, player_x, frame);
                break;
            case ObjType::BreakableRockL3:
                update_breakable_rock_l3(e);
                break;
            case ObjType::ProjectileL3:
                update_projectile_l3(e, player_x);
                break;
            default:
                break;  // no-op types (stairs, foods, balloons, signs, peaks…)
        }
    }
    return out;
}

}  // namespace olduvai::systems
