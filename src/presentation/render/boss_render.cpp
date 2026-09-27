// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/render/boss_render.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "systems/boss_l2.hpp"
#include "systems/boss_l4.hpp"
#include "systems/boss_l6.hpp"

namespace olduvai::presentation {

using formats::Rgb;
using formats::Sprite;
using namespace olduvai::systems;

bool rgb_close(const Rgb& a, int r, int g, int b) {
    return std::abs(a.r - r) <= 8 && std::abs(a.g - g) <= 8 &&
           std::abs(a.b - b) <= 8;
}

// Shared boss player render: body (death frames never flip; halo-tinted
// palette during fly-in), club, invuln flash 26/27 at (x-5, y-11).

void render_boss_player_fb(RenderTarget& t, const BossPlayerState& p,
                           const std::vector<Sprite>& atlas,
                           const std::vector<Rgb>& pal) {
    if (p.lives < 0) return;
    std::vector<Rgb> player_pal = pal;
    if (player_pal.size() > 13 && rgb_close(player_pal[13], 0, 97, 32)) {
        player_pal[13] = {130, 130, 130};
    }
    std::vector<Rgb> halo_pal = player_pal;
    if (halo_pal.size() > 5 && rgb_close(halo_pal[5], 194, 130, 97)) {
        halo_pal[5] = {130, 0, 32};
    }
    // Float base on the HD smooth path, else the integer position; the three
    // player blits add integer offsets to it.
    const float ppx = t.use_float_pos ? t.player_fx : static_cast<float>(p.x);
    const float ppy = t.use_float_pos ? t.player_fy : static_cast<float>(p.y);
    if (p.sprite >= 0 && p.sprite < static_cast<int>(atlas.size())) {
        const bool flip = p.death_counter > 0 ? false : p.facing_left;
        blit_sprite(t, atlas[static_cast<std::size_t>(p.sprite)],
                    p.halo_flag > 0 ? halo_pal : player_pal,
                    ppx + p.sprite_dx, ppy + p.sprite_dy, flip);
    }
    if (p.club_spr >= 0 && p.club_spr < static_cast<int>(atlas.size()) &&
        p.death_counter == 0) {
        blit_sprite(t, atlas[static_cast<std::size_t>(p.club_spr)],
                    player_pal, ppx + p.club_dx, ppy + p.club_dy,
                    p.facing_left);
    }
    if (p.hit_counter > 0 && p.hit_counter < 100) {
        const bool show = p.hit_counter > 15 || p.halo_toggle != 0;
        const int spr = 26 + (p.halo_toggle & 1);
        if (show && spr < static_cast<int>(atlas.size())) {
            blit_sprite(t, atlas[static_cast<std::size_t>(spr)],
                        player_pal, ppx - 5, ppy - 11);
        }
    }
}

void blit_bg(RenderTarget& t, const BossAssets& a) {
    if (!t.hd_path()) {
        // Classic path: t.px is a 320x200 buffer — direct copy.
        std::copy(a.bg.begin(), a.bg.end(), t.px);
    } else {
        // HD path: upscale the 320x200 bg through the cache and fill target.
        const auto& hd = t.cache->get(a.bg, 320, 200, t.scale, *t.profile);
        const std::size_t n = static_cast<std::size_t>(t.w) * t.h * 4;
        std::copy_n(hd.px.begin(), n, t.px);
    }
}

// Float-select: float render shadow on the HD smooth path, else integer logic
// value (lround of an exact int round-trips → byte-identical).

static inline float fsel(bool use_float, float f, int i) {
    return use_float ? f : static_cast<float>(i);
}

// Float x/y through blit_sprite's float overload (HD-rounded).  Integer
// callers use the int twin, which round-trips exactly.

void blit_at(RenderTarget& t, const std::vector<Sprite>& atlas, int idx,
             const std::vector<Rgb>& pal, float x, float y) {
    if (idx >= 0 && idx < static_cast<int>(atlas.size())) {
        blit_sprite(t, atlas[static_cast<std::size_t>(idx)], pal, x, y);
    }
}

void blit_at(RenderTarget& t, const std::vector<Sprite>& atlas, int idx,
             const std::vector<Rgb>& pal, int x, int y) {
    blit_at(t, atlas, idx, pal, static_cast<float>(x), static_cast<float>(y));
}

// ── L2 arena render ──────────────────────────────────────────────────────

struct JawRow { int xa, ya, sa, xb, yb, sb; };
constexpr JawRow kL2JawTable[5] = {
    {133, 50, 35, 151, 21, 30}, {133, 48, 36, 151, 20, 31},
    {133, 48, 36, 151, 20, 31}, {133, 50, 35, 151, 21, 30},
    {134, 50, 34, 151, 21, 29},
};

void render_l2_sprites(RenderTarget& t, const BossAssets& a,
                       const BossPlayerState& p, const L2BossState& boss) {
    blit_at(t, a.elem, 0, a.palette, 144, 15);       // T-Rex body
    blit_at(t, a.spr, 29, a.palette, 151, 21);       // head close-up
    // Arm: idle pose or the 5-frame swing pair.
    if (boss.arm_frame == 0) {
        blit_at(t, a.spr, 34, a.palette, 134, 50);
    } else {
        const auto& r = kL2JawTable[static_cast<std::size_t>(
            std::min(boss.arm_frame - 1, 4))];
        blit_at(t, a.spr, r.sa, a.palette, r.xa, r.ya);
        blit_at(t, a.spr, r.sb, a.palette, r.xb, r.yb);
    }
    // Jaw: closed (healthy/damaged by the 290 threshold) or open.
    if (boss.jaw_state == 0) {
        if (boss.health < 0x122) blit_at(t, a.spr, 38, a.palette, 136, 170);
        else blit_at(t, a.spr, 37, a.palette, 137, 177);
    } else if (boss.jaw_state == 1) {
        blit_at(t, a.spr, 38, a.palette, 136, 170);
    } else {
        blit_at(t, a.spr, 39, a.palette, 136, 168);
    }
    // Stomp dust by timer window.
    const int tt = boss.stomp_timer;
    if ((tt > 49 && tt < 52) || tt > 53) blit_at(t, a.spr, 32, a.palette, 155, 39);
    if (tt > 51 && tt < 54) blit_at(t, a.spr, 33, a.palette, 155, 39);
    // Projectiles.
    for (const auto& s : boss.slots) {
        const float sfx = fsel(t.use_float_pos, s.fx, s.x);
        if (s.ptype == 1) {
            const int base = s.direction == 0 ? kL2ProjSprRight : kL2ProjSprLeft;
            blit_at(t, a.spr,
                    base + kL2ProjAnim[static_cast<std::size_t>(s.frame)],
                    a.palette, sfx, static_cast<float>(kL2ProjY));
        } else if (s.ptype == 2) {
            blit_at(t, a.spr,
                    s.direction == 1 ? kL2ProjSprDyingLeft
                                     : kL2ProjSprDyingRight,
                    a.palette, sfx, static_cast<float>(kL2ProjY));
        }
    }
    render_boss_player_fb(t, p, a.spr, a.palette);
}

void render_l2_frame(RenderTarget& t, const BossAssets& a,
                     const BossPlayerState& p, const L2BossState& boss) {
    blit_bg(t, a);
    render_l2_sprites(t, a, p, boss);
}

// ── L4 arena render ──────────────────────────────────────────────────────

// Per-frame walk records: head dx/dy, body1, body2, tail (all relative to
// the boss position; sprites 0-based).
struct L4WalkRec {
    int hx, hy, b1x, b1y, b1s, b2x, b2y, b2s, tx, ty, ts;
};
constexpr L4WalkRec kL4WalkRight[5] = {
    {2, 2, -50, 0, 34, 13, 13, 35, -74, 10, 29},
    {2, 1, -50, -1, 36, 9, 10, 37, -74, 9, 29},
    {2, 2, -50, 0, 38, 13, 14, 39, -74, 10, 29},
    {2, 1, -50, -1, 40, 13, 10, 41, -74, 9, 29},
    {2, 2, -50, 0, 42, 14, 13, 43, -74, 10, 29},
};
constexpr L4WalkRec kL4WalkLeft[5] = {
    {0, 2, 50, 0, 34, 36, 13, 35, 107, 10, 29},
    {0, 1, 50, -1, 36, 23, 10, 37, 107, 9, 29},
    {0, 2, 50, 0, 38, 20, 14, 39, 107, 10, 29},
    {0, 1, 50, -1, 40, 36, 10, 41, 107, 9, 29},
    {0, 2, 50, 0, 42, 34, 13, 43, 107, 10, 29},
};
// Spring pad frames: {y, sprite 1-based} at x=150.
constexpr int kL4Spring[4][2] = {{170, 60}, {169, 61}, {168, 62}, {169, 61}};

void blit_flip(RenderTarget& t, const std::vector<Sprite>& atlas, int idx,
               const std::vector<Rgb>& pal, float x, float y, bool flip) {
    if (idx >= 0 && idx < static_cast<int>(atlas.size())) {
        blit_sprite(t, atlas[static_cast<std::size_t>(idx)], pal, x, y,
                    flip);
    }
}

void blit_flip(RenderTarget& t, const std::vector<Sprite>& atlas, int idx,
               const std::vector<Rgb>& pal, int x, int y, bool flip) {
    blit_flip(t, atlas, idx, pal, static_cast<float>(x), static_cast<float>(y),
              flip);
}

void render_l4_sprites(RenderTarget& t, const BossAssets& a,
                       const BossPlayerState& p, const L4BossState& boss) {
    const int bx = boss.boss_x;   // logic (head-select)
    // Float draw positions (smooth-motion HD path); head-sprite selection below
    // stays on the integer logic x so the threshold doesn't flicker mid-lerp.
    const float fbx = fsel(t.use_float_pos, boss.fx, boss.boss_x);
    const float fby = fsel(t.use_float_pos, boss.fy, boss.boss_y);
    const bool flip = boss.direction == 1;
    if (boss.stun_counter != 0) {
        blit_at(t, a.spr, 33, a.palette, fbx, fby);   // stunned whole-body
    } else {
        const auto& rec = (flip ? kL4WalkLeft : kL4WalkRight)[
            static_cast<std::size_t>(boss.walk_frame % 5)];
        int head_spr, pal_off;
        if (bx > 100 && bx < 170) {
            head_spr = 31;
            pal_off = 0;
        } else {
            head_spr = 32;
            pal_off = 5;
        }
        blit_flip(t, a.spr, rec.b1s, a.palette, fbx + rec.b1x, fby + rec.b1y,
                  flip);
        blit_flip(t, a.spr, rec.b2s, a.palette, fbx + rec.b2x, fby + rec.b2y,
                  flip);
        if (boss.hit_flash == 0 && boss.win_flag == 0) {
            blit_flip(t, a.spr, rec.ts, a.palette, fbx + rec.tx, fby + rec.ty,
                      flip);
        } else {
            // Horn-hit flash replaces the tail.
            if (boss.direction == 0) {
                blit_flip(t, a.spr, 30, a.palette, fbx - 55, fby + 9, false);
            } else {
                blit_flip(t, a.spr, 30, a.palette, fbx + 86, fby + 9, true);
            }
        }
        blit_flip(t, a.spr, head_spr, a.palette, fbx + rec.hx,
                  fby + pal_off + rec.hy, flip);
    }
    // Spring pad, drawn every frame (state 0 = the idle pose): FUN_24cc_020c
    // enqueues it unconditionally.
    {
        const auto& s = kL4Spring[boss.spring_state & 3];
        blit_at(t, a.spr, s[1] - 1, a.palette, 150, s[0]);
    }
    render_boss_player_fb(t, p, a.spr, a.palette);
}

void render_l4_frame(RenderTarget& t, const BossAssets& a,
                     const BossPlayerState& p, const L4BossState& boss) {
    blit_bg(t, a);
    render_l4_sprites(t, a, p, boss);
}

// ---- L6 arena render ----
// The attack-frame table is in systems/boss_l6.cpp (l6_select_hmat_parts,
// DS:0x1fe4 {0,1,2,1,0}).

void render_l6_sprites(RenderTarget& t, const BossAssets& a,
                       const BossPlayerState& p, const L6BossState& boss) {
    // The giant is two H-atlas parts (citations at l6_select_hmat_parts): body
    // (H1/H2/H3) at (80,7) (254f_0078:00c1/:01d9, col 0x50) and the arm+head
    // strip (H4) at (208,7) (:00ee/:01f8, col 0xd0).  A swing pairs the body
    // with the same table value (+3 -> H4[tbl]); a hit draws the shocked pair:
    // H1 body (:01d1) + H4[3] (:01f0, the 96x57 strip covering the right
    // shoulder).
    const L6HmatParts parts = l6_select_hmat_parts(boss);
    const std::vector<Sprite>* body = parts.body == 1 ? &a.h2
                                    : parts.body == 2 ? &a.h3
                                                      : &a.h1;
    if (body != nullptr && !body->empty()) {
        blit_at(t, *body, 0, a.palette, 80, 7);
    }
    if (!a.h4.empty()) {
        blit_at(t, a.h4,
                std::min(parts.head, static_cast<int>(a.h4.size()) - 1),
                a.palette, 208, 7);
    }
    // Ground-punch trampoline, drawn every frame (state 0 = the idle pad,
    // sprite 59): FUN_254f_0003:003b-005d enqueues it unconditionally.  The
    // lift and state advance stay gated on state > 0 (update_ground_punch).
    {
        constexpr int kPunch[4][2] = {{170, 59}, {169, 60}, {168, 61},
                                      {169, 60}};
        const auto& s = kPunch[boss.ground_punch_state & 3];
        blit_at(t, a.spr, s[1], a.palette, 110, s[0]);
    }
    render_boss_player_fb(t, p, a.spr, a.palette);
}

void render_l6_frame(RenderTarget& t, const BossAssets& a,
                     const BossPlayerState& p, const L6BossState& boss) {
    blit_bg(t, a);
    render_l6_sprites(t, a, p, boss);
}

// ---- L2 victory flash (FUN_23cf_0a20 0x0db2, 18 frames) ----
// Even frames: sprites 54-57 at the quadrant positions + closed jaw 38 at
// (136,170).  Odd: 50-53 + open jaw 39 at (136,168).  Player frozen, sprite 28.
// No lives redraw during the flash.

// Victory sprites only (no background), for a wide target at origin_x = M so
// the T-Rex stays in the centre instead of being mirrored.
// render_l2_victory_frame = blit_bg + this.

void render_l2_victory_sprites(RenderTarget& t, const BossAssets& a,
                               const BossPlayerState& p, int flash_frame) {
    blit_at(t, a.elem, 0, a.palette, 144, 15);   // ELEML2[0] static body

    if (flash_frame % 2 == 0) {
        // Even: "defeated" quads 54-57, closed jaw 38 at (136,170).
        blit_at(t, a.spr, 54, a.palette, 160, 17);
        blit_at(t, a.spr, 55, a.palette, 224, 17);
        blit_at(t, a.spr, 56, a.palette, 165, 79);
        blit_at(t, a.spr, 57, a.palette, 229, 79);
        blit_at(t, a.spr, 38, a.palette, 136, 170);
    } else {
        // Odd: "alive" quads 50-53, open jaw 39 at (136,168).
        blit_at(t, a.spr, 50, a.palette, 160, 17);
        blit_at(t, a.spr, 51, a.palette, 224, 17);
        blit_at(t, a.spr, 52, a.palette, 164, 79);
        blit_at(t, a.spr, 53, a.palette, 228, 79);
        blit_at(t, a.spr, 39, a.palette, 136, 168);
    }

    // Player frozen, sprite 28 (standing) at current position.
    blit_at(t, a.spr, 28, a.palette, p.x, p.y);
}

void render_l2_victory_frame(RenderTarget& t, const BossAssets& a,
                              const BossPlayerState& p, int flash_frame) {
    blit_bg(t, a);   // RING.PC1 backdrop
    render_l2_victory_sprites(t, a, p, flash_frame);
}

// ---- L4 victory (FUN_24cc_02f2 0x06bd-0x07a1) ----
// win_flag 1: boss walking, player sprite 28.  2: player rising, sprite 3.
// 3: rider sprite 58 at (boss_x-12, 119) replaces the player.  The boss draws
// in every phase.

// Ride-off sprites only (no background), for a wide target at origin_x = M
// so rider and dino overflow into the margins.  render_l4_victory_frame =
// blit_bg + this.

void render_l4_victory_sprites(RenderTarget& t, const BossAssets& a,
                               const BossPlayerState& p,
                               const L4BossState& boss) {
    const int bx = boss.boss_x, by = boss.boss_y;
    const bool flip = boss.direction == 1;
    // Boss walk animation (same as fight render, unconditional in EXE 0x06dd).
    if (boss.stun_counter != 0) {
        blit_at(t, a.spr, 33, a.palette, bx, by);
    } else {
        const auto& rec = (flip ? kL4WalkLeft : kL4WalkRight)[
            static_cast<std::size_t>(boss.walk_frame % 5)];
        int head_spr, pal_off;
        if (bx > 100 && bx < 170) {
            head_spr = 31; pal_off = 0;
        } else {
            head_spr = 32; pal_off = 5;
        }
        blit_flip(t, a.spr, rec.b1s, a.palette, bx + rec.b1x, by + rec.b1y, flip);
        blit_flip(t, a.spr, rec.b2s, a.palette, bx + rec.b2x, by + rec.b2y, flip);
        // win_flag != 0: the horn-hit sprite 30 replaces the tail
        // (FUN_24cc_0007 0x0192-0x01db).
        if (boss.direction == 0)
            blit_flip(t, a.spr, 30, a.palette, bx - 55, by + 9, false);
        else
            blit_flip(t, a.spr, 30, a.palette, bx + 86, by + 9, true);
        blit_flip(t, a.spr, head_spr, a.palette, bx + rec.hx,
                  by + pal_off + rec.hy, flip);
    }

    // Victory player, phases 1-3 only; win_flag >= 100 draws no standing
    // player. That frame becomes the fade source.
    if (boss.win_flag == 1) {
        // Phase 1: player standing, sprite 28 (no flip — EXE Game_EnqueueSprite
        // flag 0; boss_l4.py:628-630).
        blit_at(t, a.spr, 28, a.palette, p.x, p.y);
    } else if (boss.win_flag == 2) {
        // Phase 2: player rising, air sprite 3 (no flip — EXE flag 0;
        // boss_l4.py:634-636 blits unflipped).
        blit_at(t, a.spr, 3, a.palette, p.x, p.y);
    } else if (boss.win_flag >= 3) {
        // Phase 3 onward: sprite 58 is the rider, at the dino's x.  `>= 3`: the
        // win_flag == 100 frame is the fade source and must still show the
        // rider (the reference never renders that frame).
        blit_at(t, a.spr, 58, a.palette, boss.boss_x - 12, 119);
    }
}

void render_l4_victory_frame(RenderTarget& t, const BossAssets& a,
                              const BossPlayerState& p, const L4BossState& boss) {
    blit_bg(t, a);
    render_l4_victory_sprites(t, a, p, boss);
}

// ---- L6 victory (FUN_254f_02b5 0x0500-0x0641) ----
// Static backdrop, built once by the caller: PC1 + H1[pose2] at (80,7) +
// L6SPR[49] at (213,7), re-blitted each frame.  Player: dropping = air sprite
// 3 (flip = facing_left); landed = sprite 28 at (x,167), no flip.  Defeat
// cycle sprite 51 + cycle_idx at (208,7).

void render_l6_victory_frame(RenderTarget& t,
                              const std::vector<std::uint8_t>& victory_bg_px,
                              const BossAssets& a,
                              const BossPlayerState& p,
                              const L6BossState& boss) {
    // Reset to static victory backdrop.  victory_bg_px is always 320x200
    // regardless of scale — upscale it through the cache in HD mode.
    if (!t.hd_path()) {
        std::copy(victory_bg_px.begin(), victory_bg_px.end(), t.px);
    } else {
        const auto& hd = t.cache->get(victory_bg_px, 320, 200, t.scale,
                                      *t.profile);
        const std::size_t n = static_cast<std::size_t>(t.w) * t.h * 4;
        std::copy_n(hd.px.begin(), n, t.px);
    }

    constexpr int kFloorY = 160;
    if (p.y < kFloorY) {
        blit_flip(t, a.spr, 3, a.palette, p.x, p.y, p.facing_left);
    } else {
        blit_at(t, a.spr, 28, a.palette, p.x, 167);
    }

    // Cycling defeat sprite at (208, 7).
    blit_at(t, a.spr, 51 + boss.cycle_idx, a.palette, 208, 7);
}

// L6 victory sprites only (no background), for the widescreen overflow
// present: beaten pose (H3[0] at 80,7), face (L6SPR[49] at 213,7), the player
// (3 dropping / 28 landed), defeat cycle (51+cycle_idx at 208,7).

void render_l6_victory_sprites(RenderTarget& t, const BossAssets& a,
                               const BossPlayerState& p, const L6BossState& boss) {
    blit_at(t, a.h3, 0, a.palette, 80, 7);
    if (static_cast<int>(a.spr.size()) > 49)
        blit_sprite(t, a.spr[49], a.palette, 213, 7);
    constexpr int kFloorY = 160;
    if (p.y < kFloorY) {
        // The drop is the one moving thing: float base on the HD smooth path (4
        // px per tick is 16 output px at hd_scale 4, too coarse for three
        // sub-frames). Classic feeds exact integers.
        const float ppx =
            t.use_float_pos ? t.player_fx : static_cast<float>(p.x);
        const float ppy =
            t.use_float_pos ? t.player_fy : static_cast<float>(p.y);
        if (3 < static_cast<int>(a.spr.size()))
            blit_sprite(t, a.spr[3], a.palette, ppx, ppy, p.facing_left);
    } else {
        // Landed: EXE pins the standing sprite at y=167 and never flips it,
        // so there is nothing to interpolate.
        blit_at(t, a.spr, 28, a.palette, p.x, 167);
    }
    blit_at(t, a.spr, 51 + boss.cycle_idx, a.palette, 208, 7);
}



}  // namespace olduvai::presentation
