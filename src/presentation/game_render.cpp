// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/game_render.hpp"

#include "presentation/tile_patterns.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>

#include "core/game_tables.hpp"
#include "enhance/upscale.hpp"
#include "presentation/widescreen.hpp"   // compose_widescreen (wide static bg cache)
#include "systems/cave_logic.hpp"        // kSprCaveDescent1 (never-flip range)

namespace olduvai::presentation {

using core::Entity;
using core::MonsterState;
using core::ObjType;
using formats::Rgb;
using formats::Sprite;

namespace {

// EXE bug — matches original: the EXE hardcodes 0x69 = 105 ONE-BASED at
// 27f7:1140; our 0-based sheet makes that 104 (red arrow).  105 here would
// draw the fire monster (gameplay-confirmed, reference commit 1fe2cc1).
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
    // Background + floor tiles + secret clip-line, then the foreground pass.
    // Splitting the two lets the enhanced widescreen present compose an entity-
    // free background center, assemble the wide buffer, and draw the foreground
    // ONCE over it at origin_x = margin (so entities overflow the 320 edge) —
    // see draw_entities + draw_background.  Here both phases run; advance_state
    // defaults true so the per-frame club_flag mutation fires once.
    draw_background(t, state, a, post_background_hook);

    // 3+. Foreground (entities, spring, hazards/popups, death halo, player).
    draw_entities(t, state, a, draw_player);
}

// Float-select: the float render shadow on the HD smooth-motion path, else the
// integer logic value (lround of an exact int round-trips → byte-identical).
static inline float fsel(bool use_float, float f, int i) {
    return use_float ? f : static_cast<float>(i);
}

void draw_entities(RenderTarget& t, systems::SystemsState& state,
                   const LevelRenderAssets& a, bool draw_player) {
    // 3. Entities.
    const auto& spr_mat = a.entity_sprites;
    for (const Entity& e : state.entities) {
        if (!e.active || !e.visible) continue;
        // Moving platform: two 16-px tile pieces at the oscillating y.
        // The EXE queues sentinel sprite 1000 (Objects_Update PLATFORM handler
        // 2A04:0c73) and the flush special-cases it into FUN_263c_0931, which
        // picks the tile pair BY LEVEL: capstone 263c:0936-0x094b
        // `cmp [0x9c6c],3` → Dark Woods uses 1-based tiles 13/15 = ELEML3[12]
        // and [14] (16x9 planks), every other level 1-based 1/3 = ELEMLx[0]
        // and [2].  Raw EXE bytes at file 0x18cf1 verified.  Drawing [0]/[2]
        // unconditionally (cyxx-derived) attached L3's 16x55 ground pillars
        // to the moving planks.
        if (e.obj_type == ObjType::Platform) {
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
            continue;
        }
        const int spr_idx = e.sprite;
        if (spr_idx < 0 || spr_idx >= static_cast<int>(spr_mat.size()))
            continue;
        // Base position: float render position (fx/fy) on the HD smooth-motion
        // path, else the integer logic position (byte-identical — lround of an
        // exact int round-trips).  All the per-type draw_x/draw_y adjustments
        // below are integer offsets added to this base, so they carry the
        // sub-pixel component through to the HD-rounded blit.
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

    // 3b. Secret-room spring/trampoline sprite.
    // EXE: drawn by L1 main at Ghidra 24199-24222, sprite 0x93 (1-based)
    // = index 146 (0-based) from L1SPR.MAT.  Fixed x=6; y=148 while the
    // bounce fires, y=164 otherwise.  Only drawn when in the secret room.
    // Draw order spec F1: after entities, before death halo + HUD.
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

    // 3c. Food-gate indicators ("NOT ENOUGH FOOD" cue): on the level-end gate
    // screen, when the food bar isn't full, the EXE draws two indicator sprites
    // either side of the gate so the player knows to go back and eat (the gate
    // itself blocks the exit until food >= 45).  EXE all gate levels:
    // FUN_263c_09ab(0,0x6e,100,0x53,0) + (0,0xad,100,0x5c,0) → sprites 82 + 91
    // at (110,100) + (173,100).  Gate screen: L3=17, L1/L5/L7=18.  L1 capstone
    // 0x0475 / 0x048f.  FOOD_GATE = 45.
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

    // 4. Hazards + popups.
    if (state.stone_state != 0 &&
        kSprFallingStone < static_cast<int>(spr_mat.size())) {
        blit_sprite(t, spr_mat[kSprFallingStone], a.palette,
                    fsel(t.use_float_pos, state.stone_fx, state.stone_x),
                    fsel(t.use_float_pos, state.stone_fy, state.stone_y));
    }
    if (state.fireball_flag != 0 &&
        kSprFallingStone < static_cast<int>(spr_mat.size())) {
        // Flip the fireball sprite to face its travel direction.  The fireball
        // and falling stone share sprite 108; the stone falls vertically (no
        // flip) but the fireball is directional.  EXE FUN_27f7_089f
        // 0x08d8-0x08dc passes palette_flag = fireball_flag-1 (0 = right, 1 =
        // left) to Game_EnqueueSprite for a flipped sprite variant; with one
        // fireball sprite the equivalent is flip_h when moving LEFT
        // (fireball_flag == 2).  Matches the Python reference + the EXE-faithful
        // fireball-gate behaviour.  Travel direction
        // is already correct (toward the player).
        blit_sprite(t, spr_mat[kSprFallingStone], a.palette,
                    fsel(t.use_float_pos, state.fireball_fx, state.fireball_x),
                    fsel(t.use_float_pos, state.fireball_fy, state.fireball_y),
                    /*flip=*/state.fireball_flag == 2);
    }
    for (const auto& b : state.score_bonuses) {
        if (b.active_this_frame &&
            b.sprite < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[static_cast<std::size_t>(b.sprite)],
                        a.palette, fsel(t.use_float_pos, b.fx, b.x),
                        fsel(t.use_float_pos, b.fy, b.y));
        }
    }

    // 4b. Death halo (+ wing on L5) rising from the death position.
    if (state.death_halo_active) {
        constexpr int kSprDeathHalo = 117, kSprDeathWing = 124;
        if (kSprDeathHalo < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[kSprDeathHalo], a.palette,
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

    // 4c. L5 screen-12 DETACHED glider fly-away (empty glider, no rider) — the
    // player has dismounted; the glider drifts up-right (handle_l5_screen12_
    // glider: glider_x += 5, glider_y -= 4 while glider_y > -30).  Body sprite
    // 117 (0x76) + chute 124, EXE capstone 0x036d/0x0387, reference
    // _GLIDER_BODY_SPR.  Drawn here (in draw_entities) so the widescreen overflow
    // pass carries it across the margin instead of hard-clipping at x=320.
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

    // 5. Player + weapon overlay.  Skipped wholesale when composing the
    // outgoing screen-transition frame (see the header comment) — this
    // also skips the club_flag decrement below, which must fire once
    // per gameplay frame, not per compose.
    if (!draw_player) return;
    // Switch to the PLAYER-only clip (default = no clip, so non-widescreen and
    // every other caller is byte-identical).  The widescreen overflow pass sets
    // this to the no-neighbour edge so the player can't spill onto the synthetic
    // fill, while entities above kept the wide entity clip and overflow freely.
    t.clip_x_lo = t.player_clip_x_lo;
    t.clip_x_hi = t.player_clip_x_hi;
    systems::PlayerState& p = state.player;
    // Float render base on the HD smooth-motion path (else integer logic pos,
    // byte-identical).  Every player blit below is this base + integer offsets,
    // HD-rounded — 1-HD-pixel motion granularity instead of the 4-HD-pixel snap.
    const float px = t.use_float_pos ? t.player_fx : static_cast<float>(p.x);
    const float py = t.use_float_pos ? t.player_fy : static_cast<float>(p.y);
    // Post-hit invulnerability halo/shield overlay.  // FUN_27f7_12c7
    // Visibility (capstone 0x12ff-0x130b): skip the blink-off phase —
    // show = (hit_counter > 15) OR (hit_blink != 0).
    // L5 glider branch (DS:0x989c != 0): sprite 0x8d at (x+6, y+10)
    // (capstone 0x134d / 0x1346); otherwise frames 127+hit_blink at
    // (x-5, y-11), x another -4 while climbing (capstone 0x132b).
    const auto draw_halo_overlay = [&]() {
        if (p.hit_counter <= 0) return;
        if (p.hit_counter <= 15 && p.hit_blink == 0) return;
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
    };
    // Flight composites replace the walk sprite while alive in flight —
    // the halo overlay still applies (only the base sprite is swapped).
    if (state.glider_active && p.death_counter == 0) {
        if (state.current_level == 1 &&
            kSprPlayerWithBalloons < static_cast<int>(spr_mat.size())) {
            blit_sprite(t, spr_mat[kSprPlayerWithBalloons], a.palette,
                        px, py - 30);
            draw_halo_overlay();
            return;
        }
        if (state.current_level == 5) {
            // RIDING glider = flight body sprite 116 (0x75), which INCLUDES the
            // caveman on the glider — NOT 117 (0x76), the empty detached body
            // drawn during the screen-12 fly-away.  EXE Player_UpdateAndDraw
            // branch E (capstone 0x1c39) / reference _GLIDER_FLIGHT_BODY_SPR.
            // (olduvai previously used 117 here → glider looked riderless.)
            constexpr int kGliderFlightBody = 116, kGliderChute = 124;
            if (kGliderFlightBody < static_cast<int>(spr_mat.size())) {
                blit_sprite(t, spr_mat[kGliderFlightBody], a.palette, px, py);
            }
            if (kGliderChute < static_cast<int>(spr_mat.size())) {
                blit_sprite(t, spr_mat[kGliderChute], a.palette,
                            px + 21, py);
            }
            draw_halo_overlay();
            return;
        }
    }
    // Enhanced #20 — teleport cloud phases: on the POSE bookend ticks
    // (depart 12-10, arrive 3-1) the player renders as PLAYER_TURN via
    // the shared 134 override below; on cloud/empty ticks the player is
    // fully dematerialized — no body, no halo (game_app's
    // draw_teleport_fx draws the cloud instead).
    //
    // pending_sign_teleport ALSO hides: between the departure countdown
    // hitting 0 (end-of-tick decrement) and the deferred completion (next
    // tick's logic step), both tick counters read 0 while the player is
    // still dissolved — without this term the widescreen end-of-tick
    // re-compose drew one frame of a fully materialized player after the
    // smoke (2026-07-06 regression of the fade-fix ordering).
    bool tel_pose = false;
    if ((state.teleport_out_ticks > 0 || state.teleport_in_ticks > 0 ||
         state.pending_sign_teleport) &&
        p.death_counter == 0) {
        tel_pose = state.teleport_out_ticks > 9 ||
                   (state.teleport_in_ticks > 0 &&
                    state.teleport_in_ticks <= 3);
        if (!tel_pose) return;
    }
    if (p.sprite >= 0 && p.sprite < static_cast<int>(spr_mat.size())) {
        // Cave-EMERGE animation — intentional divergence (owner ruling
        // 2026-07-05, same class as the in-game-font tally screens): the
        // DOS EXE restores from a cave with a bare fade and no emerge
        // frames; the owner deems that a DOS oversight vs the Amiga port.
        // After exit_cave the player renders kSprPlayerTurn (134,
        // front-facing; catalog-verified PLAYER_TURN, FUN_27f7_1b51
        // 0x1da1/0x1e2b `mov [bp-2], 0x87`).  Enhanced v2 pacing (matches
        // the Enhanced #20 teleport idiom): 9 ticks = 3 dim stages held 3
        // ticks each (1/3 → 2/3 → full thirds of the palette), player
        // frozen meanwhile (frame_runner gate).  Classic: 2 lit ticks,
        // draw-only, physics/input untouched.  +2 aligns the ink centre
        // (13) with STAND's (15).
        int draw_sprite = p.sprite;
        int emerge_dx = 0;
        bool emerge_active = false;
        int emerge_dim_num = 3;   // palette scale numerator (of 3)
        if ((state.cave_emerge_frames > 0 || tel_pose) &&
            p.death_counter == 0) {
            draw_sprite = systems::kSprPlayerTurn;
            emerge_dx = 2;
            emerge_active = true;
            // Enhanced v2 3-stage reveal, 3-tick holds: frames 9-7 →
            // 1/3, 6-4 → 2/3, 3-1 → full (thirds formula, clamped so the
            // teleport POSE ticks — frames == 0 — stay full-bright).
            if (state.enhanced_active && state.cave_emerge_frames > 0)
                emerge_dim_num = std::min(
                    3, 1 + (systems::kCaveEmergeTicksEnhanced -
                            state.cave_emerge_frames) /
                               systems::kCaveEmergeStageHold);
        }
        // Cave-descent sprites (back view, 44-46) never flip either:
        // FUN_27f7_1b51's descent block (1b63-1b8b) is one unconditional
        // enqueue — `xor di, di` at 1b58 zeroes the flag; no facing branch.
        // The art is left-packed (ink 0-21; the +4 at 1b7f centres it), so
        // mirroring shifts the figure +10 px right — the owner-reported
        // "snap right on re-enter after a left-facing cave exit".
        const bool never_flip =
            emerge_active ||
            p.death_counter > 0 || draw_sprite == kSprPlayerGhost1 ||
            draw_sprite == kSprPlayerGhost1 + 1 ||
            (draw_sprite >= systems::kSprCaveDescent1 &&
             draw_sprite <= systems::kSprCaveDescent1 + 2);
        const bool flip = never_flip ? false : (p.facing_left != 0);
        std::vector<formats::Rgb> dim_pal;
        const std::vector<formats::Rgb>* pal = &a.palette;
        if (emerge_dim_num < 3) {
            dim_pal = a.palette;
            for (auto& c : dim_pal) {
                c.r = static_cast<std::uint8_t>(c.r * emerge_dim_num / 3);
                c.g = static_cast<std::uint8_t>(c.g * emerge_dim_num / 3);
                c.b = static_cast<std::uint8_t>(c.b * emerge_dim_num / 3);
            }
            pal = &dim_pal;
        }
        blit_sprite(t, spr_mat[static_cast<std::size_t>(draw_sprite)],
                    *pal, px + p.dx + emerge_dx, py + p.dy, flip);
        draw_halo_overlay();

        // Weapon overlay + the club_flag decrement (the decrement lives in
        // the weapon DRAW, after rendering).  // FUN_27f7_1f72
        //
        // The DRAW must run in every pass (so the club sprite overflows into
        // the widescreen margin too), but the STATE MUTATIONS — the death/
        // cave-warp clear and the post-draw decrement — must fire EXACTLY ONCE
        // per gameplay frame.  The single authoritative advance is the main fb
        // compose (advance_state defaults true); the widescreen entity-overflow
        // pass sets t.advance_state = false so its draw shows the SAME club_flag
        // (identical sprite) without double-advancing it.
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
}

void draw_mirrored_lava_bubbles(RenderTarget& t,
                                const systems::SystemsState& state,
                                const LevelRenderAssets& a, bool mirror_left,
                                bool mirror_right) {
    if (!mirror_left && !mirror_right) return;
    const auto& spr = a.entity_sprites;
    // Continue the lava pool's activity into the no-neighbour margin by
    // TRANSLATION — a copy of each bubble shifted by two lavarock tiles
    // (128 px, keeping phase with the 64-wide pool tiling), NOT a
    // reflection: the earlier mirror produced twin bubbles rising
    // symmetrically around the seam (user: "just a mirror effect, visible
    // seam"), while the translated copy reads as the same pool going on.
    // Handedness preserved (no flip).  origin_x = margin places it in the
    // margin and blit clips any off-margin part.  Float fx → smooth-motion
    // sub-pixel.
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
