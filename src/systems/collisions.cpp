// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/collisions.hpp"

#include "core/game_tables.hpp"

#include <cstdlib>
#include <set>

namespace olduvai::systems {

using core::CollisionResult;
using core::Entity;
using core::MonsterState;
using core::ObjType;

namespace {

constexpr int kSprBirdKo = 43;

// Per-monster X-right damage bound (data-table word 5).
int monster_x_right(ObjType t) {
    switch (t) {
        case ObjType::RedDino: return 30;
        case ObjType::YellowFuzz: return 20;
        case ObjType::BrownBear: return 20;
        case ObjType::GreenDino: return 60;
        case ObjType::MonsterL3A: return 30;
        case ObjType::MonsterL5A: return 25;
        case ObjType::MonsterL5B: return 49;
        case ObjType::MonsterL7A: return 30;
        case ObjType::MonsterL7B: return 30;
        default: return 30;
    }
}

// Per-type KO body-collect score (data-table word 7).  L7B scores 0 — the
// enemy is a fellow caveman and the table author zeroed it; the score
// helper's popup search also fails for 0, so no popup either.
int monster_ko_score(ObjType t) {
    switch (t) {
        case ObjType::RedDino: return 100;
        case ObjType::YellowFuzz: return 500;
        case ObjType::BrownBear: return 200;
        case ObjType::GreenDino: return 500;
        case ObjType::MonsterL3A: return 500;
        case ObjType::MonsterL3B: return 100;
        case ObjType::MonsterL5A: return 100;
        case ObjType::MonsterL5B: return 500;
        case ObjType::MonsterL7A: return 500;
        case ObjType::MonsterL7B: return 0;   // cannibal rule
        default: return 100;
    }
}

bool is_monster(ObjType t) {
    switch (t) {
        case ObjType::RedDino: case ObjType::YellowFuzz:
        case ObjType::BrownBear: case ObjType::GreenDino:
        case ObjType::MonsterL3A: case ObjType::MonsterL3B:
        case ObjType::MonsterL5A: case ObjType::MonsterL5B:
        case ObjType::MonsterL7A: case ObjType::MonsterL7B:
            return true;
        default: return false;
    }
}

bool is_chimp(ObjType t) {
    return t == ObjType::Chimp || t == ObjType::ChimpL5;
}

// SECRET_FOOD score by sprite number is game data — read from the user's
// executable at startup into core::game_tables().secret_scores (DS:0x8094).

void add_score_event(CollisionResult& r, int x, int y, int v) {
    r.score_events.push_back({x, y, v});
}

// ── club-hit handlers (second swing frame) ───────────────────────────────

bool club_hidden_food(Entity& e, const CollisionContext& c,
                      CollisionResult& r) {
    if (e.state <= 1) return false;
    const int px = c.player_x, py = c.player_y;
    if (px + 10 > e.x && px - 10 < e.x && py + 15 > e.y && py - 10 < e.y) {
        --e.state;
        r.monster_hit = true;
        if (e.state == 1) e.visible = true;
        return true;
    }
    return false;
}

bool club_monster(Entity& e, const CollisionContext& c, CollisionResult& r) {
    if (e.state != static_cast<int>(MonsterState::HeadingPlayer)) return false;
    const int px = c.player_x, py = c.player_y;
    bool x_hit;
    if (c.facing_left) {
        x_hit = (e.x < px && e.x + e.club_reach_left > px);
    } else {
        x_hit = (px + e.club_reach_right > e.x && px + 3 < e.x);
    }
    if (x_hit && py + 30 > e.y && py - 15 < e.y) {
        e.direction = c.facing_left ? 0 : 1;
        r.monster_hit = true;
        e.state = static_cast<int>(MonsterState::RunningAway);
        e.state_counter = 0;
        return true;
    }
    return false;
}

bool club_bonus(Entity& e, const CollisionContext& c, CollisionResult& r) {
    if (!e.visible || !(e.counter >= 21 && e.counter < 65)) return false;
    const int px = c.player_x, py = c.player_y;
    if (e.x + 35 > px && e.x - 40 < px && e.y - 29 < py && e.y + 20 > py &&
        c.club_flag == 1) {
        e.mask = 0x80 | (e.mask & 0x7F);   // rise mode, keep the type bits
        e.counter = 65;
        e.bonus_rise_dy = -20;
        e.bonus_rise_y = e.y + 10;
        r.monster_hit = true;
        return true;
    }
    return false;
}

bool club_egg(Entity& e, const CollisionContext& c, CollisionResult& r) {
    if (e.counter <= 0) return false;
    const int h = (c.level == 5 || c.level == 7) ? 13 : 9;
    if (e.y - h != c.player_y) return false;
    const int px = c.player_x;
    const bool x_hit = c.facing_left ? (e.x + 30 > px && e.x < px)
                                     : (e.x > px && e.x - 35 < px);
    if (!x_hit) return false;
    --e.counter;
    if (e.counter > 0 && c.axe_flag) --e.counter;
    r.monster_hit = true;
    if (e.counter == 0) {
        e.active = false;
        e.visible = false;
        ++r.food_collected;
        r.score_gained += 50;
        add_score_event(r, e.x, e.y - 4, 50);
    }
    return true;
}

bool club_rock(Entity& e, const CollisionContext& c, CollisionResult& r) {
    if (e.counter <= 0) return false;
    if (e.y - 2 != c.player_y) return false;
    const int px = c.player_x;
    const bool x_hit = c.facing_left ? (e.x + 35 > px && e.x < px)
                                     : (px + 40 > e.x && e.x > px);
    if (!x_hit) return false;
    --e.counter;
    if (e.counter > 0 && c.axe_flag) --e.counter;
    r.monster_hit = true;
    return true;
}

bool club_breakable_rock(Entity& e, const CollisionContext& c,
                         CollisionResult& r) {
    if (e.counter <= 0) return false;
    if (e.y - 7 != c.player_y) return false;
    const int px = c.player_x;
    const bool x_hit = c.facing_left ? (e.x + 50 > px && px > e.x)
                                     : (px < e.x && e.x - 40 < px);
    if (!x_hit) return false;
    --e.counter;
    if (e.counter > 0 && c.axe_flag) --e.counter;
    r.monster_hit = true;
    if (e.counter == 0) {
        ++r.food_collected;
        r.score_gained += 50;
        add_score_event(r, e.x, e.y - 4, 50);
    }
    return true;
}

bool club_bird(Entity& e, const CollisionContext& c, CollisionResult& r) {
    // Knockback + sound only — no score (the helper call is SFX-only).
    if (e.state != 1) return false;
    if (c.club_flag == 0) return false;
    if (c.facing_left) return false;   // right-facing swings only
    const int px = c.player_x, py = c.player_y;
    if (!(e.x + 47 > px && e.x - 52 < px)) return false;
    if (!(e.y + 29 > py && e.y - 29 < py)) return false;
    e.state = 2;
    e.bird_anim = 0;
    e.sprite = kSprBirdKo;
    r.monster_hit = true;
    return true;
}

bool club_chimp(Entity& e, const CollisionContext& c, CollisionResult& r) {
    const int ko_state = (e.obj_type == ObjType::ChimpL5) ? 7 : 3;
    if (e.state >= ko_state) return false;
    const int px = c.player_x, py = c.player_y;
    if (e.x + 0x1E > px && e.x - 0x24 < px && e.y - 0x1E < py &&
        e.y + 0x1E > py) {
        e.state = ko_state;
        r.monster_hit = true;
        r.score_gained += 100;
        add_score_event(r, e.x, e.y - 4, 100);
        return true;
    }
    return false;
}

bool club_cave_bat(Entity& e, const CollisionContext& c, CollisionResult& r) {
    if (e.mask & 2) return false;
    const int px = c.player_x, py = c.player_y;
    if (py - 10 >= e.y) return false;
    bool hit = false;
    if (!c.facing_left) {
        if (px < e.x && e.x - 35 < px) hit = true;
    } else {
        if (e.x + 50 > px && px > e.x) hit = true;
    }
    if (hit) {
        e.mask |= 2;
        e.active = false;
        e.visible = false;
        // Silent kill — the bat club branch plays no hit sample.
        r.score_gained += 10;
        add_score_event(r, e.x, e.y, 10);
    }
    return hit;
}

// ── body-overlap handlers ────────────────────────────────────────────────

void check_climb(const Entity& e, const CollisionContext& c,
                 CollisionResult& r) {
    if (std::abs(c.player_x - e.x) >= 8) return;
    if (c.key_up) {
        if (c.climbing && c.player_y - e.y_bottom < 3) {
            r.climb_exit_top = true;
            r.climb_exit_y = e.y_bottom - 1;
        }
        if (e.y_top == c.player_y) {
            r.can_climb = true;
            r.climb_x = e.x + 4;
            r.climb_y_top = e.y_top;
            r.climb_y_bottom = e.y_bottom;
        }
    }
    if (c.key_down) {
        if (e.y_bottom == c.player_y) {
            r.can_climb = true;
            r.climb_x = e.x + 4;
            r.climb_y_top = e.y_top;
            r.climb_y_bottom = e.y_bottom;
        }
        if (c.climbing && e.y_top - c.player_y < 3) {
            r.climb_exit_bottom = true;
        }
    }
}

void check_cave_entrance(const Entity& e, const CollisionContext& c,
                         CollisionResult& r) {
    if (!c.key_down) return;   // requires holding DOWN
    if (e.x - 8 < c.player_x && e.x + 8 > c.player_x && e.y == c.player_y) {
        r.cave_enter = e.counter;
    }
}

void check_cave_sign(const Entity& e, const CollisionContext& c,
                     CollisionResult& r) {
    if (c.cave_exit_x >= 0 && c.player_x > c.cave_exit_x - 8) {
        r.cave_sign_screen = e.counter;
        r.cave_sign_x = e.init_x;
        r.cave_sign_y = e.init_y;
    }
}

void check_hidden_food_pickup(Entity& e, const CollisionContext& c,
                              CollisionResult& r) {
    if (e.state != 1) return;
    const int food_cx = e.x + 40;
    const int px = c.player_x, py = c.player_y;
    if (px + 20 > food_cx && px - 20 < food_cx && py + 25 > e.y &&
        py - 10 < e.y) {
        e.active = false;
        e.visible = false;
        e.state = 0;
        ++r.food_collected;
        r.score_gained += 100;
        add_score_event(r, food_cx, e.y - 4, 100);
    }
}

void check_lava_spring(const Entity& e, const CollisionContext& c,
                       CollisionResult& r) {
    const int px = c.player_x, py = c.player_y;
    if (e.x - 10 < px && px < e.x + 20 && py == e.y - 12 &&
        c.gravity_flag == 0) {
        r.peak_l7_spring = true;
        r.peak_l7_x_delta = 5;
        r.peak_l7_y_delta = -10;
        r.peak_l7_y_vel = 0xA8;   // 168
    }
}

void check_fire(const Entity& e, const CollisionContext& c,
                CollisionResult& r) {
    const int px = c.player_x, py = c.player_y;
    if (px > e.x - 22 && px < e.x + 7 && py > e.y - 20) r.damage = 1;
}

void check_fish(const Entity& e, const CollisionContext& c,
                CollisionResult& r) {
    const int px = c.player_x, py = c.player_y;
    if (px > e.x - 23 && px < e.x + 15 && py > e.y - 25 && py < e.y + 25) {
        r.damage = std::max(r.damage, 2);
    }
}

void check_bird(const Entity& e, const CollisionContext& c,
                CollisionResult& r) {
    if (e.state != 1) return;
    const int px = c.player_x, py = c.player_y;
    if (px > e.x - 30 && px < e.x + 10 && py > e.y - 20 && py < e.y + 18) {
        r.damage = std::max(r.damage, 2);
    }
}

void check_food_pickup(Entity& e, const CollisionContext& c,
                       CollisionResult& r) {
    if (e.state != 1) return;
    const int food_y = e.y - e.hit_h;
    const int px = c.player_x;
    if (px + 20 > e.x && px - 20 < e.x && c.player_y + 25 > food_y &&
        c.player_y - 10 < food_y) {
        e.active = false;
        e.visible = false;
        e.state = 0;
        if (e.obj_type == ObjType::SecretFood) {
            // Score + the cross-handler fallthrough into the food++ block.
            const int sv = (e.spr_num >= 0 && e.spr_num < 10)
                               ? core::game_tables().secret_scores[
                                     static_cast<std::size_t>(e.spr_num)]
                               : 10;
            r.score_gained += sv;
            if (sv > 0) add_score_event(r, e.x, food_y - 4, sv);
            ++r.food_collected;
        } else {
            // FOOD_CAVE: the entire effect is the bonus activation.
            r.bonus_type = e.spr_num;
        }
    }
}

void check_animated_food(const Entity& e, const CollisionContext& c,
                         CollisionResult& r) {
    if (e.spr_num == 0) return;
    const int px = c.player_x, py = c.player_y;
    if (px > e.x - 20 && px < e.x + 18 && py + 29 > e.y - 1) r.damage = 1;
}

void check_rock_damage(const Entity& e, const CollisionContext& c,
                       CollisionResult& r) {
    if (e.counter <= 0) return;
    const int px = c.player_x, py = c.player_y;
    if (e.y > py && e.y - 35 < py && px + 25 > e.x && e.x + 25 > px) {
        r.damage = 10;
    }
}

void check_monster_body(Entity& e, const CollisionContext& c,
                        CollisionResult& r) {
    const int px = c.player_x, py = c.player_y;
    if (e.state == static_cast<int>(MonsterState::Ko) && e.ko_counter > 0) {
        // L3B gets a widened second pass (+30 vs +23) for the big bird body.
        const int y_max = (e.obj_type == ObjType::MonsterL3B) ? 30 : 23;
        if (e.x + 20 > px && e.x - 10 < px && e.y - 30 < py &&
            e.y + y_max > py) {
            e.state = static_cast<int>(MonsterState::Dead);
            ++r.food_collected;   // unconditional, even for score 0
            const int score = monster_ko_score(e.obj_type);
            r.score_gained += score;
            if (score > 0) add_score_event(r, e.x, e.y, score);
        }
        return;
    }
    if (e.state == static_cast<int>(MonsterState::HeadingPlayer)) {
        if (e.obj_type == ObjType::MonsterL3B) {
            // Fixed asymmetric AABB (-20/+25) that does NOT mirror with the
            // sprite facing — an authentic original quirk.
            if (e.x - 20 < px && px < e.x + 25 && e.y - 30 < py &&
                py < e.y + 30) {
                r.damage = 2;
            }
            return;
        }
        const int x_right = monster_x_right(e.obj_type);
        if (px + 20 > e.x && px - x_right < e.x && py + 10 > e.y &&
            py - 15 < e.y) {
            r.damage = 2;
        }
    }
}

void check_cave_spider(const Entity& e, const CollisionContext& c,
                       CollisionResult& r) {
    const int di = e.spider_di;
    if (di <= 0) return;
    const int px = c.player_x, py = c.player_y;
    if (px > e.x - 23 && px < e.x + 16 && py > e.y + 25 &&
        py < e.y + di + 16) {
        r.damage = 1;   // shared epilogue with the fire handler
    }
}

void check_cave_bat(const Entity& e, const CollisionContext& c,
                    CollisionResult& r) {
    if (e.mask & 2) return;
    const int px = c.player_x, py = c.player_y;
    if (px < e.x && px > e.x - 30 && py - 4 < e.y) r.damage = 1;
}

void check_chimp_l7(const Entity& e, const CollisionContext& c,
                    CollisionResult& r) {
    const int px = c.player_x, py = c.player_y;
    if (e.x - 23 < px && px < e.x + 15 && e.y - 25 < py && py < e.y + 25) {
        r.damage = std::max(r.damage, 2);
    }
}

void check_jumping_fish(const Entity& e, const CollisionContext& c,
                        CollisionResult& r) {
    // AABB uses the direction-shifted draw x, not the raw position.
    const int si = (e.direction == 0) ? e.x - 3 : e.x + 3;
    const int px = c.player_x, py = c.player_y;
    if (si - 30 < px && px < si + 10 && e.y - 10 < py && py < e.y + 20) {
        r.damage = std::max(r.damage, 2);
    }
}

void check_snake(const Entity& e, const CollisionContext& c,
                 CollisionResult& r) {
    const int px = c.player_x, py = c.player_y;
    const int cnt = e.counter;
    int head_sprite;
    if (cnt == 11 || cnt == 20) head_sprite = 0x98;
    else if (cnt >= 12 && cnt <= 19) head_sprite = 0x99;
    else head_sprite = 0x97;
    if (head_sprite == 0x97) {   // flat phase: upper box
        if (e.x + 15 < px && px < e.x + 44 && e.y - 9 < py &&
            py < e.y + 25) {
            r.damage = std::max(r.damage, 2);
        }
    } else {                     // striking: lower box
        if (e.x + 14 < px && px < e.x + 51 && e.y + 23 < py &&
            py < e.y + 53) {
            r.damage = std::max(r.damage, 2);
        }
    }
}

void check_projectile(const Entity& e, const CollisionContext& c,
                      CollisionResult& r) {
    const int px = c.player_x, py = c.player_y;
    if (e.proj_x > px - 2 && e.proj_x < px + 24 && e.proj_y > py - 2 &&
        e.proj_y < py + 20) {
        r.damage = std::max(r.damage, 1);
    }
}

void check_platform(const Entity& e, const CollisionContext& c,
                    CollisionResult& r) {
    const int px = c.player_x, py = c.player_y;
    if (e.x - 20 < px && e.x + 20 > px && py + 31 > e.y && py + 5 < e.y) {
        r.platform_y = e.current_y;
    }
}

void check_chimp_projectile(Entity& e, const CollisionContext& c,
                            CollisionResult& r) {
    if (e.throw_flag == 0) return;
    const int tx = e.throw_x, ty = e.throw_y;
    const int px = c.player_x, py = c.player_y;
    if (tx < px + 20 && tx > px - 10 && ty + 12 > py && ty < py + 23) {
        r.damage = (e.obj_type == ObjType::ChimpL7) ? 2 : 1;
        e.throw_flag = 0;
    }
}

}  // namespace

CollisionResult check_player_collisions(std::vector<Entity>& entities,
                                        const CollisionContext& ctx) {
    CollisionResult result;
    std::set<const Entity*> club_hit;

    // ── phase 1: club hits (second swing frame only) ──
    if (ctx.club_flag == 1) {
        for (Entity& e : entities) {
            if (!e.active) continue;
            bool hit = false;
            if (e.obj_type == ObjType::HiddenFood) {
                hit = club_hidden_food(e, ctx, result);
            } else if (is_monster(e.obj_type)) {
                hit = club_monster(e, ctx, result);
            } else if (e.obj_type == ObjType::AncestorGhost) {
                hit = club_bonus(e, ctx, result);
            } else if (e.obj_type == ObjType::Egg) {
                hit = club_egg(e, ctx, result);
            } else if (e.obj_type == ObjType::Rock) {
                hit = club_rock(e, ctx, result);
            } else if (e.obj_type == ObjType::BreakableRockL3) {
                hit = club_breakable_rock(e, ctx, result);
            } else if (e.obj_type == ObjType::Bird) {
                hit = club_bird(e, ctx, result);
            } else if (is_chimp(e.obj_type)) {
                hit = club_chimp(e, ctx, result);
            } else if (e.obj_type == ObjType::CaveBat) {
                hit = club_cave_bat(e, ctx, result);
            }
            if (hit) club_hit.insert(&e);
        }
    }

    // ── phase 2: body overlaps ──
    for (Entity& e : entities) {
        if (!e.active || club_hit.count(&e)) continue;
        const ObjType t = e.obj_type;

        if (t == ObjType::Stairs || t == ObjType::VineL3) {
            check_climb(e, ctx, result);
            continue;
        }
        if (t == ObjType::CaveEntrance) { check_cave_entrance(e, ctx, result); continue; }
        if (t == ObjType::CaveSign) { check_cave_sign(e, ctx, result); continue; }
        if (t == ObjType::HiddenFood) { check_hidden_food_pickup(e, ctx, result); continue; }

        if (!e.visible) continue;

        if (t == ObjType::Peak) {
            // Spike damage when the player stands on top.
            const int px = ctx.player_x, py = ctx.player_y;
            if (e.x - 21 < px && px < e.x + 5 && py + 27 > e.y &&
                e.y - 16 > py) {
                result.damage = std::max(result.damage, 1);
            }
            continue;
        }
        if (t == ObjType::PeakL7) { check_lava_spring(e, ctx, result); continue; }
        if (t == ObjType::Fire) { check_fire(e, ctx, result); continue; }
        if (t == ObjType::Fish) { check_fish(e, ctx, result); continue; }
        if (t == ObjType::Bird) { check_bird(e, ctx, result); continue; }
        if (t == ObjType::JumpingFishL5) { check_jumping_fish(e, ctx, result); continue; }
        if (t == ObjType::ChimpL7) { check_chimp_l7(e, ctx, result); continue; }
        if (t == ObjType::SecretFood || t == ObjType::FoodCave) {
            check_food_pickup(e, ctx, result);
            continue;
        }
        if (t == ObjType::AnimatedFoodL3) { check_animated_food(e, ctx, result); continue; }
        if (t == ObjType::Egg || t == ObjType::BreakableRockL3 ||
            t == ObjType::AncestorGhost) {
            continue;
        }
        if (t == ObjType::Rock) { check_rock_damage(e, ctx, result); continue; }
        if (is_monster(t)) { check_monster_body(e, ctx, result); continue; }
        if (t == ObjType::CaveSpider) { check_cave_spider(e, ctx, result); continue; }
        if (t == ObjType::CaveBat) { check_cave_bat(e, ctx, result); continue; }
        if (t == ObjType::Platform) { check_platform(e, ctx, result); continue; }
        if (t == ObjType::ProjectileL3) { check_projectile(e, ctx, result); continue; }
        if (t == ObjType::SnakeL3) { check_snake(e, ctx, result); continue; }
    }

    // ── phase 3: chimp projectiles ──
    for (Entity& e : entities) {
        if (is_chimp(e.obj_type)) check_chimp_projectile(e, ctx, result);
    }
    return result;
}

}  // namespace olduvai::systems
