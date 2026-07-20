// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/spawning.hpp"

#include "core/constants.hpp"
#include "core/game_tables.hpp"
#include "core/rng.hpp"

namespace olduvai::systems {

using core::Entity;
using core::MonsterState;
using core::ObjType;

namespace {

constexpr int kSprTrampoline = 129;
constexpr int kSprPeakL7 = 146;
constexpr int kSprEgg = 125;
constexpr int kSprRock = 143;
constexpr int kSprRockL3 = 116;
constexpr int kSprFishUp = 83;
constexpr int kSprJFishRiseLeft = 55;
constexpr int kSprChimp = 92;
constexpr int kSprSpider = 118;
constexpr int kSprBat = 121;
constexpr int kSprSnake = 150;
constexpr int kSprFireCave = 25;
constexpr int kSprBirdFlyBase = 40;
constexpr int kSprProjectileL3Default = 153;
constexpr int kSprHiddenFoodBase = 60;
constexpr int kSprSecretFoodBase = 61;
constexpr int kBonusSprites[6] = {90, 81, 89, 130, 131, 139};

int w(const prepare::ObjectRecord& r, std::size_t i) {
    return i < r.words.size() ? r.words[i] : 0;
}

std::uint16_t next15() { return core::global_rng().next(); }

}  // namespace

MonsterTables MonsterTables::from_exe(const std::vector<std::uint8_t>& exe) {
    MonsterTables out;
    for (const auto& ref : prepare::monster_table_refs()) {
        out.main[ref.type] = prepare::read_monster_table(exe, ref.ds_offset);
        if (ref.alt_ds_offset != 0) {
            out.alt[ref.type] =
                prepare::read_monster_table(exe, ref.alt_ds_offset);
        }
    }
    return out;
}

std::vector<Entity> spawn_screen_entities(
    const std::vector<prepare::ObjectRecord>& records,
    const MonsterTables& tables, int cave_index) {
    std::vector<Entity> out;

    for (const auto& r : records) {
        Entity e;
        e.obj_type = static_cast<ObjType>(r.type);

        switch (r.type) {
            case 0x01:    // STAIRS — invisible climb zone
            case 0x19: {  // VINE_L3 — same shape
                e.x = w(r, 0);
                e.y_top = w(r, 1);
                e.y_bottom = w(r, 2);
                e.y = e.y_top;
                e.visible = false;
                break;
            }
            case 0x02: {  // PEAK trampoline
                e.x = w(r, 0); e.y = w(r, 1);
                e.sprite = kSprTrampoline;
                e.hit_w = 16; e.hit_h = 16;
                break;
            }
            case 0x25: {  // PEAK_L7 lava spring
                e.x = w(r, 0); e.y = w(r, 1);
                e.sprite = kSprPeakL7;
                e.hit_w = 16; e.hit_h = 16;
                break;
            }
            case 0x03: {  // EGG — counter forced to 2 at reset
                e.x = w(r, 0); e.y = w(r, 1);
                // EXE bug — matches original: FUN_27f7_02cb TYPE 0x03
                // CS:0x0439 forces counter := 2 unconditionally (spawn
                // records carry 0) — hatch timer starts 2 frames in.
                e.counter = 2;
                e.sprite = kSprEgg;
                e.hit_w = 16; e.hit_h = 16;
                break;
            }
            case 0x04: {  // ROCK — counter forced to 3
                e.x = w(r, 0); e.y = w(r, 1);
                // EXE bug — matches original: FUN_27f7_02cb TYPE 0x04
                // CS:0x0367 forces counter := 3 (shake/drop starts mid-timer).
                e.counter = 3;
                e.sprite = kSprRock;
                e.hit_w = 16; e.hit_h = 16;
                break;
            }
            case 0x1C: {  // BREAKABLE_ROCK_L3 — shares the egg reset (2 HP)
                e.x = w(r, 0); e.y = w(r, 1);
                e.counter = 2;
                e.sprite = kSprRockL3;
                e.hit_w = 16; e.hit_h = 16;
                break;
            }
            case 0x05: {  // ANCESTOR_GHOST — counter may be negative (delay)
                e.x = w(r, 0); e.y = w(r, 1);
                e.counter = w(r, 2);
                e.frame_counter = w(r, 2);
                e.mask = 0;
                e.visible = false;
                e.hit_w = 40; e.hit_h = 30;
                break;
            }
            case 0x06: {  // HIDDEN_FOOD — state forced to 2 (dormant)
                e.x = w(r, 0); e.y = w(r, 1);
                e.spr_num = w(r, 2);
                e.state = 2;
                e.sprite = kSprHiddenFoodBase + e.spr_num;
                e.hit_w = 16; e.hit_h = 16;
                e.visible = false;
                break;
            }
            case 0x07: {  // SECRET_FOOD
                e.x = w(r, 0); e.y = w(r, 1);
                e.spr_num = w(r, 2);
                e.state = w(r, 3);
                e.sprite = kSprSecretFoodBase + e.spr_num;
                e.hit_w = 16; e.hit_h = 16;
                break;
            }
            case 0x18: {  // ANIMATED_FOOD_L3 hazard
                e.x = w(r, 0); e.y = w(r, 1);
                e.spr_num = w(r, 2);
                e.counter = 0;
                constexpr int kAnim[4] = {94, 84, 83, 84};
                e.sprite = kAnim[e.spr_num & 3];
                e.hit_w = 16; e.hit_h = 16;
                break;
            }
            case 0x14: {  // FOOD_CAVE — bonus sprite table
                e.x = w(r, 0); e.y = w(r, 1);
                e.spr_num = w(r, 2);
                e.state = w(r, 3);
                e.sprite = (e.spr_num >= 0 && e.spr_num < 6)
                               ? kBonusSprites[e.spr_num]
                               : kSprSecretFoodBase + e.spr_num;
                e.hit_w = 16; e.hit_h = 16;
                break;
            }
            case 0x08: {  // BALLOONS — two entities from one record
                e.x = w(r, 0); e.y = w(r, 1);
                e.sprite = w(r, 2) - 1;    // 1-based on disk
                e.hit_w = 24; e.hit_h = 24;
                out.push_back(e);
                Entity e2;
                e2.obj_type = ObjType::Balloons;
                e2.x = w(r, 3); e2.y = w(r, 4);
                e2.sprite = w(r, 5) - 1;
                e2.hit_w = 24; e2.hit_h = 24;
                out.push_back(e2);
                continue;
            }
            case 0x09: {  // FISH — y forced 220, dy = LCG%5+16, state 0
                e.x = w(r, 0);
                e.y = 220; e.init_y = 220;
                e.state = 0;
                e.dy = next15() % 5 + 16;
                e.sprite = kSprFishUp;
                e.visible = false;
                e.hit_w = 24; e.hit_h = 16;
                break;
            }
            case 0x1F: {  // JUMPING_FISH_L5 — dy then x from the LCG
                e.dy = next15() % 5 + 16;
                e.y = 220; e.init_y = 220;
                e.state = 0;
                e.x = next15() % 0xD2;
                // obj[+a] direction word (0=left, 1=right) — the EXE reads
                // it from the spawn record (FUN_27f7_02cb TYPE 0x1f).  The
                // Python oracle's extractor doesn't map this word (its
                // _FOOD_FIELDS layout ends at `state`), so it always gets 0;
                // reading the raw record here is the more EXE-faithful side.
                e.direction = w(r, 5);
                e.sprite = kSprJFishRiseLeft;
                e.hit_w = 24; e.hit_h = 16;
                break;
            }
            case 0x0A: case 0x0B: case 0x0C: case 0x0D:
            case 0x1A: case 0x1B: case 0x21: case 0x22:
            case 0x26: case 0x27: {  // shared-machine monsters
                e.init_state = w(r, 0);
                e.respawns = w(r, 1);
                e.init_x = w(r, 2);
                e.init_y = w(r, 3);
                e.x = e.init_x;
                e.y = e.init_y;
                e.state = static_cast<int>(MonsterState::Reset);
                e.direction = w(r, 8);
                e.state_counter = w(r, 9);
                e.ko_counter = w(r, 10);
                e.hit_w = 32; e.hit_h = 32;
                const auto it = tables.main.find(r.type);
                if (it != tables.main.end()) {
                    const auto& mt = it->second;
                    e.sprite = mt.init_spr;
                    e.init_spr = mt.init_spr;
                    e.spr_num = mt.move_spr;
                    e.walk_offsets.assign(mt.walk_offsets.begin(),
                                          mt.walk_offsets.end());
                    e.away_spr = mt.away_spr;
                    e.ko_spr = mt.ko_spr;
                    e.hits_to_ko = mt.energy;
                    e.dat00 = mt.dat00;
                    e.probe_di = mt.di;
                    e.probe_si = mt.si;
                    e.club_reach_right = mt.dat02;
                    e.club_reach_left = mt.var16;
                    e.direction_flag = mt.direction_flag;
                }
                const auto alt = tables.alt.find(r.type);
                if (alt != tables.alt.end()) {
                    e.alt_spr_num = alt->second.move_spr;
                    e.alt_away_spr = alt->second.away_spr;
                    e.alt_walk_offsets.assign(alt->second.walk_offsets.begin(),
                                              alt->second.walk_offsets.end());
                }
                if (e.init_state > 0) {  // pre-activated from screen entry
                    e.state = e.init_state;
                    e.visible = true;
                } else {
                    e.visible = false;
                }
                break;
            }
            case 0x0E:    // CHIMP
            case 0x20: {  // CHIMP_L5 snowman
                e.state = w(r, 0);
                e.x = w(r, 1); e.y = w(r, 2);
                e.throw_flag = w(r, 3);
                e.throw_x = w(r, 4); e.throw_y = w(r, 5);
                e.sprite = kSprChimp; e.spr_num = kSprChimp;
                e.hit_w = 24; e.hit_h = 24;
                break;
            }
            case 0x24: {  // CHIMP_L7 fish-arc: x from record word 0
                e.x = w(r, 0);
                e.y = 220;
                e.dy = next15() % 5 + 16;
                e.state = 0;
                e.anchor_a = w(r, 4);
                e.anchor_b = w(r, 5);
                e.sprite = kSprFishUp;
                e.hit_w = 24; e.hit_h = 24;
                break;
            }
            case 0x0F: {  // BIRD — waiting, four flight altitudes
                e.state = 0;
                e.bird_heights = {w(r, 4), w(r, 5), w(r, 6), w(r, 7)};
                e.bird_height_idx = 0;
                e.bird_anim = 0;
                e.spr_num = kSprBirdFlyBase;
                e.sprite = kSprBirdFlyBase;
                e.visible = false;  // state 0 must not draw
                e.hit_w = 24; e.hit_h = 24;
                break;
            }
            case 0x23: {  // PTERIYAKI_L7 — LCG x/wing + phase sub-counters
                const int x_max = w(r, 0);
                const int x_min = w(r, 1);
                const int range = (x_max - x_min) > 0 ? (x_max - x_min) : 1;
                e.x = next15() % range + x_min;
                e.anchor_a = x_max;
                e.anchor_b = x_min;
                e.wing_x = next15() % range + x_min;
                e.body_phase = 0x38;
                e.wing_phase = 0x4D;
                e.body_subcounter = next15() % 2;
                e.wing_subcounter = next15() % 2;
                e.y = 159;
                e.sprite = e.body_phase;
                e.body_sprite = e.wing_phase;
                e.body_x = e.wing_x;
                e.body_y = 165;
                e.visible = true;
                e.hit_w = 0; e.hit_h = 0;  // cosmetic, no damage
                break;
            }
            case 0x10: {  // PLATFORM — dy forced 0, current_y := y
                e.x = w(r, 0); e.y = w(r, 1);
                e.y_top = w(r, 3);
                e.y_bottom = w(r, 4);
                e.dy = 0;
                e.current_y = e.y;
                e.direction = w(r, 5);
                e.speed = w(r, 6);  // per-record speed (verbatim, may be 0)
                e.sprite = -1000;  // platform draws via tile sprites
                e.hit_w = 32; e.hit_h = 8;
                break;
            }
            case 0x12: {  // CAVE_ENTRANCE — invisible trigger
                e.x = w(r, 0); e.y = w(r, 1);
                e.counter = w(r, 2);
                e.visible = false;
                e.hit_w = 32; e.hit_h = 32;
                break;
            }
            case 0x13: {  // FIRE — note field order x, state, y
                e.x = w(r, 0);
                e.state = w(r, 1);
                e.y = w(r, 2);
                e.sprite = kSprFireCave; e.spr_num = kSprFireCave;
                e.hit_w = 16; e.hit_h = 24;
                break;
            }
            case 0x15: {  // CAVE_SIGN — destination coords + screen number
                e.counter = w(r, 0);   // destination screen
                e.init_x = w(r, 1);
                e.init_y = w(r, 2);
                e.visible = false;
                break;
            }
            case 0x16: {  // CAVE_SPIDER — phase from the LCG
                e.x = w(r, 0);
                e.y = w(r, 2);         // field order x, counter, y
                e.init_y = e.y;
                e.counter = next15() % 15;
                e.state = 0;
                e.sprite = kSprSpider; e.spr_num = kSprSpider;
                e.hit_w = 16; e.hit_h = 16;
                break;
            }
            case 0x17: {  // CAVE_BAT — bounds from the cave width table
                e.x = w(r, 0);
                e.y = w(r, 1);
                e.state = w(r, 2);
                e.sprite = kSprBat; e.spr_num = kSprBat;
                e.hit_w = 16; e.hit_h = 16;
                e.y_top = 40;  // cave left bound
                e.y_bottom = (cave_index >= 0 &&
                              cave_index < static_cast<int>(
                                  core::game_tables().cave_sizes.size()))
                                 ? core::game_tables().cave_sizes[static_cast<std::size_t>(
                                       cave_index)] - 8
                                 : core::kGameW - 8;
                break;
            }
            case 0x1D: {  // SNAKE_L3 — strike phase from the LCG
                e.x = w(r, 0); e.y = w(r, 1);
                e.counter = next15() % 10;
                e.sprite = kSprSnake; e.spr_num = kSprSnake;
                e.hit_w = 24; e.hit_h = 16;
                break;
            }
            case 0x1E: {  // PROJECTILE_L3 launcher
                e.x = w(r, 1);          // field order: state, x, y, …
                e.y = w(r, 2);
                e.init_x = e.x;
                e.init_y = e.y;
                e.state = 0;
                e.proj_x = e.x + 5;
                e.proj_y = e.y + 5;
                e.launcher_spr = 146;
                e.sprite = kSprProjectileL3Default;
                e.visible = true;
                e.hit_w = 24; e.hit_h = 20;
                break;
            }
            default: {  // fallback: place at (x, y), no behaviour
                e.x = w(r, 0);
                e.y = w(r, 1);
                e.visible = true;
                break;
            }
        }
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace olduvai::systems
