// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/game_render.hpp"

#include "presentation/render/tile_patterns.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>

#include "core/game_tables.hpp"
#include "enhance/upscale.hpp"
#include "presentation/render/widescreen.hpp"   // compose_widescreen (wide static bg cache)
#include "systems/cave_logic.hpp"        // kSprCaveDescent1 (never-flip range)

namespace olduvai::presentation {

using core::Entity;
using core::MonsterState;
using core::ObjType;
using formats::Rgb;

namespace {

// EXE quirk kept: 27f7:1140 hardcodes 0x69 = 105, one-based, so 104 in our
// 0-based sheet (red arrow); 105 would draw the fire monster.
constexpr int kSprKoArrow = 104;
constexpr int kSprPlayerGhost1 = 38;
constexpr int kSprFallingStone = 108;   // also the fireball sprite
constexpr int kSprPlayerWithBalloons = 116;
constexpr int kSprPlayerHalo1 = 127;    // shield overlay, frames 127/128
constexpr int kSprHaloLevel5 = 140;     // L5 glider hit flash, 0x8d 1-based

// Weapon overlay tables: {dx, dy, sprite} for club_flag 2 then 1.
struct WeaponFrame { int dx, dy, spr; };
constexpr WeaponFrame kClubTbl[2] = {{14, -15, 13}, {20, 3, 6}};
constexpr WeaponFrame kAxeTbl[2] = {{14, -15, 138}, {20, 3, 136}};

// Bottom-aligned entity types (record y = foot baseline).
bool bottom_aligned(ObjType t) {
    return t == ObjType::SecretFood || t == ObjType::FoodCave;
}

}  // namespace

void compose_frame(RenderTarget& t, systems::SystemsState& state,
                   const LevelRenderAssets& a, bool draw_player,
                   const std::function<void(RenderTarget&)>& post_background_hook) {
    // Background (+ floor tiles, secret clip), then the foreground.  The
    // widescreen present runs the two separately; here both run and club_flag
    // advances once.
    draw_background(t, state, a, post_background_hook);

    // 3+. Foreground (entities, spring, hazards/popups, death halo, player).
    draw_entities(t, state, a, draw_player);
}

// Float-select: the float render shadow on the HD smooth-motion path, else the
// integer logic value (lround of an exact int round-trips → byte-identical).
static inline float fsel(bool use_float, float f, int i) {
    return use_float ? f : static_cast<float>(i);
}

// Moving platform: two 16 px tile pieces at the oscillating y.  The EXE queues
// sentinel sprite 1000 (PLATFORM handler 2A04:0c73) and FUN_263c_0931 picks the
// pair by level (263c:0936-0x094b tests [0x9c6c] against 3): Dark Woods
// ELEML3[12]/[14] (16x9 planks), every other level ELEMLx[0]/[2].
static void draw_moving_platform(RenderTarget& t,
                                 const systems::SystemsState& state,
                                 const LevelRenderAssets& a, const Entity& e) {
    const std::size_t ta = state.current_level == 3 ? 12u : 0u;
    const std::size_t tb = state.current_level == 3 ? 14u : 2u;
    if (a.tile_sprites.size() > tb) {
        const float pcy = fsel(t.use_float_pos, e.f_current_y,
                               e.current_y);
        const float pbx =
            fsel(t.use_float_pos, e.fx, e.x);   // x usually static
        blit_sprite(t, a.tile_sprites[ta], a.palette, pbx, pcy);
        blit_sprite(t, a.tile_sprites[tb], a.palette, pbx + 16, pcy);
    }
}

// One entity's sprite and what is drawn with it: the two-part L3 snake and L7
// pteriyaki (body and head placed separately), the launcher it was fired
// from, a chimp's rock in flight.
static void draw_entity_composite(RenderTarget& t,
                                  const LevelRenderAssets& a,
                                  const Entity& e, int spr_idx,
                                  float draw_x, float draw_y, bool flip) {
    const auto& spr_mat = a.entity_sprites;
    const bool separate_body_head =
        e.obj_type == ObjType::SnakeL3 && e.body_sprite >= 0;
    if (!separate_body_head) {
        blit_sprite(t, spr_mat[static_cast<std::size_t>(spr_idx)],
                    a.palette, draw_x, draw_y, flip);
    }
    if (e.obj_type == ObjType::PteriyakiL7 && e.body_sprite >= 0 &&
        e.body_sprite < static_cast<int>(spr_mat.size())) {
        blit_sprite(t, spr_mat[static_cast<std::size_t>(e.body_sprite)],
                    a.palette, e.body_x, e.body_y);
    } else if (separate_body_head &&
               e.body_sprite < static_cast<int>(spr_mat.size())) {
        blit_sprite(t, spr_mat[static_cast<std::size_t>(e.body_sprite)],
                    a.palette, e.body_x, e.body_y);
        blit_sprite(t, spr_mat[static_cast<std::size_t>(spr_idx)],
                    a.palette, e.head_x, e.head_y);
    }
    if (e.launcher_spr >= 0 &&
        e.launcher_spr < static_cast<int>(spr_mat.size())) {
        blit_sprite(t, spr_mat[static_cast<std::size_t>(e.launcher_spr)],
                    a.palette, e.init_x, e.init_y);
    }
    // Chimp projectile in flight.
    if ((e.obj_type == ObjType::Chimp || e.obj_type == ObjType::ChimpL5) &&
        e.throw_flag != 0) {
        constexpr int kSprChimpThrow = 95;
        if (kSprChimpThrow < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[kSprChimpThrow], a.palette,
                        fsel(t.use_float_pos, e.f_throw_x, e.throw_x),
                        fsel(t.use_float_pos, e.f_throw_y, e.throw_y));
        }
    }
}

void draw_entity_list(RenderTarget& t, systems::SystemsState& state,
                      const LevelRenderAssets& a) {
    // 3. Entities.
    const auto& spr_mat = a.entity_sprites;
    for (const Entity& e : state.entities) {
        if (!e.active || !e.visible) continue;
        if (e.obj_type == ObjType::Platform) {
            draw_moving_platform(t, state, a, e);
            continue;
        }
        const int spr_idx = e.sprite;
        if (spr_idx < 0 || spr_idx >= static_cast<int>(spr_mat.size()))
            continue;
        // Base position: fx/fy on the HD smooth path, else the integer
        // position.  The per-type offsets below are integers added to it, so
        // the sub-pixel part reaches the blit.
        const float bx = t.use_float_pos ? e.fx : static_cast<float>(e.x);
        const float by = t.use_float_pos ? e.fy : static_cast<float>(e.y);
        float draw_x = bx, draw_y = by;
        const int spr_h = spr_mat[static_cast<std::size_t>(spr_idx)].height;
        const bool is_ko_frame =
            e.probe_si > 0 && e.ko_spr > 0 &&
            (spr_idx == e.ko_spr || spr_idx == e.ko_spr + 1);
        if (e.draw_dy != 0) {
            draw_y = by + fsel(t.use_float_pos, e.f_draw_dy,
                               e.draw_dy);   // spider / snowman windup
        } else if (is_ko_frame) {
            draw_y = by + e.probe_si - spr_h;   // KO foot alignment
            // KO arrow blink — only on the visible KO frame.
            if (e.state == static_cast<int>(MonsterState::Ko) &&
                e.ko_counter < 20 && spr_idx == e.ko_spr + 1 &&
                kSprKoArrow < static_cast<int>(spr_mat.size())) {
                blit_sprite(t, spr_mat[kSprKoArrow], a.palette,
                            bx + (e.dat00 - 3), draw_y - 8);
            }
        }
        if (e.obj_type == ObjType::HiddenFood && e.state == 1)
            draw_x = bx + 40;
        if (bottom_aligned(e.obj_type)) draw_y = by - spr_h;
        const bool flip = is_ko_frame ? false : e.facing_left;

        draw_entity_composite(t, a, e, spr_idx, draw_x, draw_y, flip);
    }

}

void draw_secret_spring(RenderTarget& t, systems::SystemsState& state,
                      const LevelRenderAssets& a) {
    const auto& spr_mat = a.entity_sprites;
    // 3b. Secret-room spring: L1 main draws sprite 0x93 (1-based) = 146 from
    // L1SPR.MAT at x=6, y=148 while bouncing, else y=164.  After entities,
    // before the death halo and HUD.
    if (state.secret_flag) {
        constexpr int kSprSecretSpring = 146;   // EXE 0x93 1-based
        constexpr int kSpringX = 6;             // EXE const at 0x1425b
        constexpr int kSpringYIdle = 164;       // EXE 0xa4
        constexpr int kSpringYBounce = 148;     // EXE 0x94
        const int spring_y =
            state.secret_spring_bouncing ? kSpringYBounce : kSpringYIdle;
        if (kSprSecretSpring < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[kSprSecretSpring], a.palette,
                        kSpringX, spring_y);
        }
    }

}

void draw_food_gate_cue(RenderTarget& t, systems::SystemsState& state,
                      const LevelRenderAssets& a) {
    const auto& spr_mat = a.entity_sprites;
    // 3c. Food-gate cue: on the gate screen with food < 45 the EXE draws
    // sprites 82 + 91 either side of the gate at (110,100) and (173,100)
    // (FUN_263c_09ab(0,0x6e,100,0x53,0) + (0,0xad,100,0x5c,0); L1 0x0475 /
    // 0x048f).  Gate screen: L3 17, L1/L5/L7 18.
    if (!a.enhanced_vector_banners &&
        (state.current_level == 1 || state.current_level == 3 ||
         state.current_level == 5 || state.current_level == 7) &&
        state.current_screen == (state.current_level == 3 ? 17 : 18) &&
        state.food_count < 45) {
        constexpr int kSprGateLeft = 82;    // EXE 0x53 (83 1-based)
        constexpr int kSprGateRight = 91;   // EXE 0x5c (92 1-based)
        if (kSprGateRight < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[static_cast<std::size_t>(kSprGateLeft)],
                        a.palette, 110, 100);
            blit_sprite(t, spr_mat[static_cast<std::size_t>(kSprGateRight)],
                        a.palette, 173, 100);
        }
    }

}

void draw_hazards_and_popups(RenderTarget& t, systems::SystemsState& state,
                      const LevelRenderAssets& a) {
    const auto& spr_mat = a.entity_sprites;
    // 4. Hazards + popups.
    if (state.stone_state != 0 &&
        kSprFallingStone < static_cast<int>(spr_mat.size())) {
        blit_sprite(t, spr_mat[kSprFallingStone], a.palette,
                    fsel(t.use_float_pos, state.stone_fx, state.stone_x),
                    fsel(t.use_float_pos, state.stone_fy, state.stone_y));
    }
    if (state.fireball_flag != 0 &&
        kSprFallingStone < static_cast<int>(spr_mat.size())) {
        // The fireball shares sprite 108 with the falling stone but is
        // directional: flip when moving left (fireball_flag == 2).
        // FUN_27f7_089f 0x08d8-0x08dc passes fireball_flag-1 as the flip flag.
        blit_sprite(t, spr_mat[kSprFallingStone], a.palette,
                    fsel(t.use_float_pos, state.fireball_fx, state.fireball_x),
                    fsel(t.use_float_pos, state.fireball_fy, state.fireball_y),
                    /*flip_h=*/state.fireball_flag == 2);
    }
    for (const auto& b : state.score_bonuses) {
        if (b.active_this_frame &&
            b.sprite < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[static_cast<std::size_t>(b.sprite)],
                        a.palette, fsel(t.use_float_pos, b.fx, b.x),
                        fsel(t.use_float_pos, b.fy, b.y));
        }
    }

}

void draw_death_halo(RenderTarget& t, systems::SystemsState& state,
                      const LevelRenderAssets& a) {
    const auto& spr_mat = a.entity_sprites;
    // 4b. Death halo (+ wing on L5) rising from the death position.
    if (state.death_halo_active) {
        constexpr int kSprDeathWing = 124;
        if (kSprBalloonBunch < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[kSprBalloonBunch], a.palette,
                        fsel(t.use_float_pos, state.death_halo_fx,
                             state.death_halo_x),
                        fsel(t.use_float_pos, state.death_halo_fy,
                             state.death_halo_y));
        }
        if (state.current_level == 5 &&
            kSprDeathWing < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[kSprDeathWing], a.palette,
                        fsel(t.use_float_pos, state.death_halo_fx,
                             state.death_halo_x) + 21,
                        fsel(t.use_float_pos, state.death_halo_fy,
                             state.death_halo_y));
        }
    }

}

void draw_l5_glider_flyaway(RenderTarget& t, systems::SystemsState& state,
                      const LevelRenderAssets& a) {
    const auto& spr_mat = a.entity_sprites;
    // 4c. L5 screen-12 detached glider (no rider) drifting up-right while
    // glider_y > -30: body 117 (0x76) + chute 124 (0x036d / 0x0387).  Drawn
    // here so the widescreen overflow pass carries it into the margin.
    if (state.current_level == 5 && state.current_screen == 12 &&
        state.glider_y > -30) {
        constexpr int kGliderBody = 117, kGliderChute = 124;
        const float gx = fsel(t.use_float_pos, state.glider_fx, state.glider_x);
        const float gy = fsel(t.use_float_pos, state.glider_fy, state.glider_y);
        if (kGliderBody < static_cast<int>(spr_mat.size()))
            blit_sprite(t, spr_mat[kGliderBody], a.palette, gx, gy);
        if (kGliderChute < static_cast<int>(spr_mat.size()))
            blit_sprite(t, spr_mat[kGliderChute], a.palette, gx + 21, gy);
    }

}

// Post-hit halo.  // FUN_27f7_12c7
// Shown when hit_counter > 15 or hit_blink != 0 (0x12ff-0x130b).  L5 glider
// (DS:0x989c != 0): sprite 0x8d at (x+6, y+10) (0x134d / 0x1346); otherwise
// frames 127+hit_blink at (x-5, y-11), x -4 more while climbing (0x132b).
static void draw_player_halo(RenderTarget& t, const systems::SystemsState& state,
                             const LevelRenderAssets& a, float px, float py) {
    const systems::PlayerState& p = state.player;
    if (p.hit_counter <= 0) return;
    if (p.hit_counter <= 15 && p.hit_blink == 0) return;
    const auto& spr_mat = a.entity_sprites;
    int idx;
    float hx, hy;
    if (state.glider_active && state.current_level == 5) {
        idx = kSprHaloLevel5;
        hx = px + 6;
        hy = py + 10;
    } else {
        idx = kSprPlayerHalo1 + p.hit_blink;
        hx = px - 5 - (p.climbing != 0 ? 4 : 0);
        hy = py - 11;
    }
    if (idx >= 0 && idx < static_cast<int>(spr_mat.size())) {
        blit_sprite(t, spr_mat[static_cast<std::size_t>(idx)],
                    a.palette, hx, hy);
    }
}

// Flight composites replace the walk sprite while alive in flight (the halo
// still applies).  Returns true when it drew the player.
static bool draw_flight_composite(RenderTarget& t,
                                  const systems::SystemsState& state,
                                  const LevelRenderAssets& a,
                                  float px, float py) {
    const auto& spr_mat = a.entity_sprites;
    if (!state.glider_active || state.player.death_counter != 0) return false;
    if (state.current_level == 1 &&
        kSprPlayerWithBalloons < static_cast<int>(spr_mat.size())) {
        blit_sprite(t, spr_mat[kSprPlayerWithBalloons], a.palette,
                    px, py - 30);
        draw_player_halo(t, state, a, px, py);
        return true;
    }
    if (state.current_level == 5) {
        // Riding glider: sprite 116 (0x75, includes the caveman), not 117 (the
        // empty body of the screen-12 fly-away).  Player_UpdateAndDraw branch E
        // (0x1c39).
        constexpr int kGliderFlightBody = 116, kGliderChute = 124;
        if (kGliderFlightBody < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[kGliderFlightBody], a.palette, px, py);
        }
        if (kGliderChute < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[kGliderChute], a.palette,
                        px + 21, py);
        }
        draw_player_halo(t, state, a, px, py);
        return true;
    }
    return false;
}

// What the body draw shows this tick: normally the physics sprite, but the
// cave-EMERGE animation and the teleport POSE bookend ticks both override it.
struct PlayerBodyPose {
    int sprite;       // sprite index to blit
    int dx;           // extra x offset (ink centring for the override)
    bool overridden;  // an override pose — never flipped
    int dim_num;      // palette scale numerator, of 3 (3 = full bright)
};

// Cave-emerge pose, an intentional divergence (the DOS EXE fades back with no
// emerge frames; the Amiga port has them): after exit_cave the player shows
// kSprPlayerTurn (134, front-facing; FUN_27f7_1b51 0x1da1/0x1e2b store 0x87).
// Enhanced: 9 ticks, 3 dim stages held 3 ticks each (1/3, 2/3, full), player
// frozen.  Classic: 2 lit ticks, draw only.  +2 aligns the ink centre (13)
// with STAND's (15).
static PlayerBodyPose player_body_pose(const systems::SystemsState& state,
                                       bool tel_pose) {
    const systems::PlayerState& p = state.player;
    PlayerBodyPose pose{p.sprite, 0, false, 3};
    if ((state.cave_emerge_frames <= 0 && !tel_pose) || p.death_counter != 0)
        return pose;
    pose.sprite = systems::kSprPlayerTurn;
    pose.dx = 2;
    pose.overridden = true;
    // Frames 9-7 -> 1/3, 6-4 -> 2/3, 3-1 -> full (clamped: teleport pose ticks
    // at frames == 0 stay full).
    if (state.enhanced_active && state.cave_emerge_frames > 0)
        pose.dim_num = std::min(
            3, 1 + (systems::kCaveEmergeTicksEnhanced -
                    state.cave_emerge_frames) /
                       systems::kCaveEmergeStageHold);
    return pose;
}

static std::vector<formats::Rgb> dimmed_palette(
    const std::vector<formats::Rgb>& pal, int num) {
    std::vector<formats::Rgb> out = pal;
    for (auto& c : out) {
        c.r = static_cast<std::uint8_t>(c.r * num / 3);
        c.g = static_cast<std::uint8_t>(c.g * num / 3);
        c.b = static_cast<std::uint8_t>(c.b * num / 3);
    }
    return out;
}

// Weapon overlay and the club_flag decrement, which sits in the weapon draw.
// // FUN_27f7_1f72
// The draw runs in every pass (the club overflows into the margin too); the
// death/cave-warp clear and the decrement only when t.advance_state.
static void draw_weapon_overlay(RenderTarget& t, systems::SystemsState& state,
                                const LevelRenderAssets& a,
                                float px, float py) {
    const auto& spr_mat = a.entity_sprites;
    systems::PlayerState& p = state.player;
    if (p.death_counter != 0 || p.cave_warp_freeze != 0) {
        if (t.advance_state) p.club_flag = 0;
    } else if (p.club_flag > 0) {
        const auto& tbl = state.halo_flight_flag ? kAxeTbl : kClubTbl;
        const auto& f = tbl[static_cast<std::size_t>(2 - p.club_flag)];
        int wdx = f.dx;
        if (p.facing_left) wdx = -wdx;
        if (f.spr < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[static_cast<std::size_t>(f.spr)],
                        a.palette, px + wdx, py + f.dy,
                        p.facing_left != 0);
        }
        if (t.advance_state) --p.club_flag;
    }
}

void draw_player_overlay(RenderTarget& t, systems::SystemsState& state,
                      const LevelRenderAssets& a, bool draw_player) {
    const auto& spr_mat = a.entity_sprites;
    // 5. Player + weapon.  Skipped for a transition's outgoing frame, which
    // also skips the club_flag decrement.
    if (!draw_player) return;
    // Player-only clip (no clip by default; the widescreen overflow pass sets
    // the no-neighbour edge).
    t.clip_x_lo = t.player_clip_x_lo;
    t.clip_x_hi = t.player_clip_x_hi;
    const systems::PlayerState& p = state.player;
    // Float base on the HD smooth path, else the integer position; every blit
    // below adds integer offsets to it.
    const float px = t.use_float_pos ? t.player_fx : static_cast<float>(p.x);
    const float py = t.use_float_pos ? t.player_fy : static_cast<float>(p.y);
    if (draw_flight_composite(t, state, a, px, py)) return;
    // Teleport clouds: on the pose ticks (depart 12-10, arrive 3-1) the player
    // is PLAYER_TURN (the 134 override below); on cloud/empty ticks nothing is
    // drawn (draw_teleport_fx draws the cloud).  pending_sign_teleport also
    // hides: both counters read 0 between the departure's end-of-tick decrement
    // and the deferred completion, and the widescreen re-compose would show one
    // frame of a whole player.
    bool tel_pose = false;
    if ((state.teleport_out_ticks > 0 || state.teleport_in_ticks > 0 ||
         state.pending_sign_teleport) &&
        p.death_counter == 0) {
        tel_pose = state.teleport_out_ticks > 9 ||
                   (state.teleport_in_ticks > 0 &&
                    state.teleport_in_ticks <= 3);
        if (!tel_pose) return;
    }
    if (p.sprite < 0 || p.sprite >= static_cast<int>(spr_mat.size())) return;
    const PlayerBodyPose pose = player_body_pose(state, tel_pose);
    // Cave-descent sprites (44-46) never flip: FUN_27f7_1b51's descent block
    // (1b63-1b8b) enqueues unconditionally (flip zeroed at 1b58).  The art is
    // left-packed, so a flip would shift the figure 10 px right.
    const bool never_flip =
        pose.overridden ||
        p.death_counter > 0 || pose.sprite == kSprPlayerGhost1 ||
        pose.sprite == kSprPlayerGhost1 + 1 ||
        (pose.sprite >= systems::kSprCaveDescent1 &&
         pose.sprite <= systems::kSprCaveDescent1 + 2);
    const bool flip = never_flip ? false : (p.facing_left != 0);
    std::vector<formats::Rgb> dim_pal;
    const std::vector<formats::Rgb>* pal = &a.palette;
    if (pose.dim_num < 3) {
        dim_pal = dimmed_palette(a.palette, pose.dim_num);
        pal = &dim_pal;
    }
    blit_sprite(t, spr_mat[static_cast<std::size_t>(pose.sprite)],
                *pal, px + p.dx + pose.dx, py + p.dy, flip);
    draw_player_halo(t, state, a, px, py);
    draw_weapon_overlay(t, state, a, px, py);
}

void draw_entities(RenderTarget& t, systems::SystemsState& state,
                   const LevelRenderAssets& a, bool draw_player) {
    draw_entity_list(t, state, a);
    draw_secret_spring(t, state, a);
    draw_food_gate_cue(t, state, a);
    draw_hazards_and_popups(t, state, a);
    draw_death_halo(t, state, a);
    draw_l5_glider_flyaway(t, state, a);
    draw_player_overlay(t, state, a, draw_player);
}

void draw_mirrored_lava_bubbles(RenderTarget& t,
                                const systems::SystemsState& state,
                                const LevelRenderAssets& a, bool mirror_left,
                                bool mirror_right) {
    if (!mirror_left && !mirror_right) return;
    const auto& spr = a.entity_sprites;
    // Continue the lava pool into the no-neighbour margin by translation (each
    // bubble shifted two lavarock tiles, 128 px, in phase with the 64 px
    // tiling), not reflection, which gave twin bubbles and a visible seam.  No
    // flip; the blit clips anything outside the margin.
    constexpr float kPoolPeriod = 128.0f;
    auto mirror_blit = [&](int idx, float ex, float ey, bool orig_flip) {
        if (idx < 0 || idx >= static_cast<int>(spr.size())) return;
        if (mirror_left)
            blit_sprite(t, spr[static_cast<std::size_t>(idx)], a.palette,
                        ex - kPoolPeriod, ey, orig_flip);
        if (mirror_right)
            blit_sprite(t, spr[static_cast<std::size_t>(idx)], a.palette,
                        ex + kPoolPeriod, ey, orig_flip);
    };
    for (const Entity& e : state.entities) {
        if (e.obj_type != ObjType::PteriyakiL7) continue;
        if (!e.active || !e.visible) continue;
        const float ex = t.use_float_pos ? e.fx : static_cast<float>(e.x);
        const float ey = t.use_float_pos ? e.fy : static_cast<float>(e.y);
        mirror_blit(e.sprite, ex, ey, e.facing_left != 0);
        if (e.body_sprite >= 0)
            mirror_blit(e.body_sprite, static_cast<float>(e.body_x),
                        static_cast<float>(e.body_y), false);
    }
}

void compose_frame(FrameBuffer& fb, systems::SystemsState& state,
                   const LevelRenderAssets& a, bool draw_player,
                   const std::function<void(RenderTarget&)>& post_background_hook) {
    RenderTarget t{fb.px.data(), fb.w, fb.h, 1, nullptr, nullptr};
    compose_frame(t, state, a, draw_player, post_background_hook);
}

}  // namespace olduvai::presentation
