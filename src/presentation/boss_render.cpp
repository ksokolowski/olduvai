// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss_render.hpp"

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
    // Float render base on the HD smooth-motion path (Part 1 follow-up), else
    // the integer logic position (byte-identical — lround of an exact int).
    // All three player blits are this base + integer offsets, HD-rounded.
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
    if (t.scale <= 1) {
        // Classic path: t.px is a 320×200 buffer — direct copy.
        std::copy(a.bg.begin(), a.bg.end(), t.px);
    } else {
        // HD path: upscale the 320×200 bg through the cache and fill target.
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

// Float x/y: routes through blit_sprite's float overload (HD-rounds the dest).
// Integer callers convert implicitly and round-trip exactly, so every existing
// fixed-position boss/scenery draw stays byte-identical; the player draws pass a
// sub-pixel render position on the smooth-motion HD path (Part 1 follow-up).

void blit_at(RenderTarget& t, const std::vector<Sprite>& atlas, int idx,
             const std::vector<Rgb>& pal, float x, float y) {
    if (idx >= 0 && idx < static_cast<int>(atlas.size())) {
        blit_sprite(t, atlas[static_cast<std::size_t>(idx)], pal, x, y);
    }
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
                    a.palette, sfx, kL2ProjY);
        } else if (s.ptype == 2) {
            blit_at(t, a.spr,
                    s.direction == 1 ? kL2ProjSprDyingLeft
                                     : kL2ProjSprDyingRight,
                    a.palette, sfx, kL2ProjY);
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
    // Spring pad — drawn EVERY frame (state 0 = the idle/compressed pose).
    // EXE FUN_24cc_020c enqueues the sprite unconditionally; the prior
    // `spring_state > 0` guard hid the idle sprite so only the expanded pose
    // appeared on a bounce (and the animation popped in).  Matches
    // boss_l4.py::_render_spring (L4_STOMP_ANIM[state & 3]).
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

// ── L6 arena render ──────────────────────────────────────────────────────
// (attack-frame table lives in systems/boss_l6.cpp::l6_select_hmat_parts —
// DS:0x1fe4 {0,1,2,1,0}, raw EXE bytes at file 0x21974)

void render_l6_sprites(RenderTarget& t, const BossAssets& a,
                       const BossPlayerState& p, const L6BossState& boss) {
    // Giant = TWO H-atlas parts (l6_select_hmat_parts carries the capstone
    // cites): part A body (H1/H2/H3) at (80,7) — 254f_0078:00c1/:01d9 col
    // 0x50; part B arm+head strip (H4 sheet) at (208,7) — :00ee/:01f8 col
    // 0xd0.  Swing window pairs body with the SAME table value (+3 atlas
    // offset → H4[tbl]); the hit reaction draws the SHOCKED PAIR — H1 body
    // (base+0, :01d1, NO table offset) + H4[3] (base+6, :01f0, the 96×57
    // strip that also covers the right shoulder).  Matches
    // boss_l6.py::_select_hmat_parts / _render_hmat.
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
    // Ground-punch trampoline — drawn EVERY frame at the current state
    // (state 0 = the idle/collapsed pad, sprite 59, that shows the player
    // where to step).  EXE FUN_254f_0003:003b-005d enqueues the sprite
    // UNCONDITIONALLY; the prior `ground_punch_state > 0` guard hid the idle
    // pad so the trampoline was invisible until triggered.  The lift + state
    // advance stay gated on state>0 in update_ground_punch (boss_l6.cpp).
    // Matches boss_l6.py::_update_ground_punch (L6_STOMP_ANIM[state]).
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

// ── L2 victory flash render (FUN_23cf_0a20 0x0db2, 18 frames) ────────────────
// Even frames: sprites 54-57 at the four quadrant positions + closed jaw 38
// at (136,170).  Odd frames: sprites 50-53 + open jaw 39 at (136,168).
// Player frozen at current x/y, sprite 28 (standing).  RING.PC1 + body blit
// each frame (no HUD lives redraw — EXE never redraws in victory flash).

// Sprite-only L2 victory pass (NO background) — drawable onto a WIDE target at
// origin_x = ws_M so the defeated T-Rex stays in the centre 320 instead of being
// mirrored into the no-neighbour margins (the "mirrored big T-Rex tail" the
// margin reflection produced when a baked 320 frame was mirror-wrapped).  The
// full-frame render_l2_victory_frame below = blit_bg + this.

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

// ── L4 victory render (FUN_24cc_02f2 0x06bd-0x07a1) ──────────────────────────
// Phase 1 (win_flag==1): boss still walking, player sprite 28 (standing).
// Phase 2 (win_flag==2): player rising, sprite 3 (air).
// Phase 3 (win_flag==3): player replaced by riding sprite 58 at (boss_x-12, 119).
// Boss always drawn in all phases.

// Sprite-only L4 ride-off victory pass (NO background) — drawable onto a WIDE
// target at origin_x = ws_M so the riding triceratops + player overflow into
// the margins instead of being baked into a 320 frame and then mirrored.  The
// full-frame render_l4_victory_frame below = blit_bg + this.

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
        // Victory render: win_flag != 0 always, so the EXE draws the HORN-HIT
        // sprite 30 in the TAIL's place (FUN_24cc_0007 0x0192-0x01db; same
        // else-branch as render_l4_sprites + boss_l4.py:521-531).  The old victory
        // render dropped this entirely → the "missing tail" during the ride-off.
        if (boss.direction == 0)
            blit_flip(t, a.spr, 30, a.palette, bx - 55, by + 9, false);
        else
            blit_flip(t, a.spr, 30, a.palette, bx + 86, by + 9, true);
        blit_flip(t, a.spr, head_spr, a.palette, bx + rec.hx,
                  by + pal_off + rec.hy, flip);
    }

    // Victory player sprite — phases 1/2/3 ONLY.  win_flag >= 100 (done) draws
    // NO player (matches boss_l4.py:625-643 — no else; the Python victory loop
    // exits at 100 without rendering that frame).  The old `else` drew the
    // standing sprite 28 at win_flag==100, and that final frame became the
    // post-victory fade source → a one-frame "player standing on the left" flash
    // before the fade-to-tally.
    if (boss.win_flag == 1) {
        // Phase 1: player standing, sprite 28 (no flip — EXE Game_EnqueueSprite
        // flag 0; boss_l4.py:628-630).
        blit_at(t, a.spr, 28, a.palette, p.x, p.y);
    } else if (boss.win_flag == 2) {
        // Phase 2: player rising, air sprite 3 (no flip — EXE flag 0;
        // boss_l4.py:634-636 blits unflipped).
        blit_at(t, a.spr, 3, a.palette, p.x, p.y);
    } else if (boss.win_flag == 3) {
        // Phase 3: riding sprite 58 at (boss_x-12, 119) — IS the player
        // (boss_l4.py:641-643; no separate player draw).
        blit_at(t, a.spr, 58, a.palette, boss.boss_x - 12, 119);
    }
}

void render_l4_victory_frame(RenderTarget& t, const BossAssets& a,
                              const BossPlayerState& p, const L4BossState& boss) {
    blit_bg(t, a);
    render_l4_victory_sprites(t, a, p, boss);
}

// ── L6 victory render (FUN_254f_02b5 0x0500-0x0641) ──────────────────────────
// Static victory backdrop: PC1 + H1[pose2] at (80,7) + L6SPR[49] at (213,7).
// Built once by caller (victory_bg); re-blitted each frame before drawing
// moving sprites (prevents smear of prior player position).
// Player: dropping = air sprite 3 (flip=facing_left); landed = sprite 28
//   at (x,167) no flip.
// Defeat cycle sprite: base 51 + cycle_idx at (208,7) throughout.

void render_l6_victory_frame(RenderTarget& t,
                              const std::vector<std::uint8_t>& victory_bg_px,
                              const BossAssets& a,
                              const BossPlayerState& p,
                              const L6BossState& boss) {
    // Reset to static victory backdrop.  victory_bg_px is always 320×200
    // regardless of scale — upscale it through the cache in HD mode.
    if (t.scale <= 1) {
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

// L6 victory SPRITES only (NO background) — for the widescreen clean-bg +
// overflow present, so the beaten giant / landed player are NOT mirror-reflected
// into the no-neighbour margins (the "big caveman reflection").  Mirrors
// render_l2/l4_victory_sprites; draws everything render_l6_victory_frame does
// except the bg copy: beaten giant pose (H3[0]@80,7), beaten face (L6SPR[49]@
// 213,7), the player (air sprite 3 dropping / sprite 28 landed), and the cycling
// defeat sprite (51+cycle_idx@208,7).

void render_l6_victory_sprites(RenderTarget& t, const BossAssets& a,
                               const BossPlayerState& p, const L6BossState& boss) {
    blit_at(t, a.h3, 0, a.palette, 80, 7);
    if (static_cast<int>(a.spr.size()) > 49)
        blit_sprite(t, a.spr[49], a.palette, 213, 7);
    constexpr int kFloorY = 160;
    if (p.y < kFloorY)
        blit_flip(t, a.spr, 3, a.palette, p.x, p.y, p.facing_left);
    else
        blit_at(t, a.spr, 28, a.palette, p.x, 167);
    blit_at(t, a.spr, 51 + boss.cycle_idx, a.palette, 208, 7);
}

// Settings values come from the user-editable play.json / the menu string
// store — never bare-stof them: parse_f (parse_util.hpp, via
// settings_preview.hpp) swallows a non-numeric value like "loud" that
// would otherwise throw through the frame loop.  The former local mirror
// collided with that include and is gone.

// flow_key_from_sym (the SDL→SettingsFlow::Key bridge) is shared with
// game_app via presentation/dialog_key_map.hpp (CC3) — previously mirrored
// here per OL-B6, now unified without pulling SDL into the pure-logic layer.

}  // namespace olduvai::presentation
