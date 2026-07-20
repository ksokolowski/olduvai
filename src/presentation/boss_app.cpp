// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss_app.hpp"

#include "presentation/gamepad.hpp"

#include <SDL.h>

#include "presentation/image_out.hpp"

#include <cstdlib>     // std::getenv (OLDUVAI_WS_FORCE_MARGIN widescreen override)
#include <algorithm>   // std::copy_n (libstdc++ needs the full header; libc++
                       // pulls it in transitively, hence macOS-only build pass)
#include <cstring>
#include <fstream>

#include "core/rng.hpp"
#include "formats/cur.hpp"
#include "enhance/enhanced_hud.hpp"
#include "enhance/hd_asset_cache.hpp"
#include "enhance/hd_text.hpp"
#include "enhance/mmpx.hpp"
#include "enhance/omniscale.hpp"
#include "enhance/upscale.hpp"
#include "formats/mdi.hpp"
#include "prepare/cache_paths.hpp"
#include "presentation/audio.hpp"
#include "presentation/bug_capture.hpp"
#include "presentation/game_render.hpp"
#include "presentation/dialog_key_map.hpp"
#include "presentation/menu.hpp"
#include "presentation/menu_model.hpp"
#include "presentation/menu_render.hpp"
#include "presentation/hud_render.hpp"
#include "presentation/replay.hpp"
#include "presentation/screens.hpp"
#include "presentation/settings_flow.hpp"
#include "presentation/smooth_present.hpp"
#include "presentation/boss_widescreen.hpp"
#include "presentation/widescreen.hpp"
#include "presentation/text_overlay.hpp"
#include "presentation/window_util.hpp"
#include "systems/boss_l2.hpp"
#include "systems/boss_l4.hpp"
#include "systems/boss_l6.hpp"

namespace olduvai::presentation {

namespace {

using formats::Rgb;
using formats::Sprite;
using namespace olduvai::systems;

std::vector<std::uint8_t> slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

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

struct BossAssets {
    std::vector<std::uint8_t> bg;   // RGBA 320x200 (pip drain mutates it)
    std::vector<Sprite> spr;        // boss sprite atlas
    std::vector<Sprite> elem;       // arena body pieces (L2)
    std::vector<Sprite> h1, h2, h3, h4; // L6 body poses A/B/C + head sheet (H4)
    std::vector<Sprite> charset;
    std::vector<Rgb> palette;
    // Pause-menu bone cursor: L1SPR[33] score-bone + FOND1 palette (same
    // pointer art the surface pause / main menu use).
    std::vector<Sprite> bone_atlas;
    std::vector<Rgb> bone_palette;
};

void erase_pip_column(BossAssets& a, int health) {
    if (health < 0 || health >= 320) return;
    for (int y = 0; y < 6; ++y) {
        const std::size_t off =
            (static_cast<std::size_t>(y) * 320 + health) * 4;
        a.bg[off] = 0;
        a.bg[off + 1] = 0;
        a.bg[off + 2] = 0;
        a.bg[off + 3] = 255;
    }
}

bool load_boss_assets(const std::filesystem::path& dir, int level,
                      BossAssets& a) {
    formats::CurArchive fa(slurp(dir / "FILESA.CUR"));
    formats::CurArchive fb2(slurp(dir / "FILESB.CUR"));
    formats::CurArchive va(slurp(dir / "FILESA.VGA"));
    formats::CurArchive vb(slurp(dir / "FILESB.VGA"));
    auto entry = [&](const std::string& n) -> const std::vector<std::uint8_t>* {
        for (formats::CurArchive* ar : {&fa, &fb2, &va, &vb}) {
            if (ar->contains(n)) return &ar->get(n).data;
        }
        return nullptr;
    };
    const auto* ring = entry("RING.PC1");
    const auto* font = entry("CHARSET1.MAT");
    if (ring == nullptr || font == nullptr) return false;
    a.charset = formats::MatFile(*font, "CHARSET1.MAT").sprites();
    if (const auto* l1s = entry("L1SPR.MAT"))
        a.bone_atlas = formats::MatFile(*l1s, "L1SPR.MAT").sprites();
    if (const auto* f1 = entry("FOND1.PC1"))
        a.bone_palette = formats::parse_pc1(*f1).palette;
    const auto img = formats::parse_pc1(*ring);
    a.palette = img.palette;
    if ((level == 2 || level == 4) && a.palette.size() > 5) {
        // DAC index-5 override → dark red.  L2: FUN_23cf 0x0a4f; L4:
        // FUN_24cc_02f2 0x031f-0x0329 writes DS:0x91b9=0x20, DS:0x8a6f=0,
        // DS:0x8e43=8 → RGB 130,0,32 — the triceratops tail-on-hit + entry
        // balloons.  RING.PC1 ships idx 5 brown; the EXE patches it red.
        // L6 deliberately stays brown (l6_boss_palette_idx5_brown_not_oversight).
        a.palette[5] = {130, 0, 32};
    }
    a.bg.assign(320 * 200 * 4, 0);
    for (std::size_t i = 0; i < img.pixels.size() && i < 320 * 200; ++i) {
        const std::uint8_t idx = img.pixels[i];
        const Rgb c = (idx < a.palette.size()) ? a.palette[idx] : Rgb{};
        a.bg[i * 4] = c.r;
        a.bg[i * 4 + 1] = c.g;
        a.bg[i * 4 + 2] = c.b;
        a.bg[i * 4 + 3] = 255;
    }
    const char* spr_name = level == 2 ? "L2SPR.MAT"
                          : level == 4 ? "L4SPR.MAT" : "L6SPR.MAT";
    if (const auto* s = entry(spr_name)) {
        a.spr = formats::MatFile(*s, spr_name).sprites();
    }
    if (level == 2) {
        if (const auto* e = entry("ELEML2.MAT")) {
            a.elem = formats::MatFile(*e, "ELEML2.MAT").sprites();
        }
    }
    if (level == 6) {
        // Body poses A/B/C are three separate single-sprite MATs (H1/H2/H3),
        // selected per attack frame; H4 holds the head sheet (3 heads +
        // shocked).  H2 (mid-swing) was previously not loaded → the whole
        // club-swing collapsed to the H1 windup pose.
        if (const auto* h = entry("H1.MAT"))
            a.h1 = formats::MatFile(*h, "H1.MAT").sprites();
        if (const auto* h = entry("H2.MAT"))
            a.h2 = formats::MatFile(*h, "H2.MAT").sprites();
        if (const auto* h = entry("H3.MAT"))
            a.h3 = formats::MatFile(*h, "H3.MAT").sprites();
        if (const auto* h = entry("H4.MAT"))
            a.h4 = formats::MatFile(*h, "H4.MAT").sprites();
    }
    return !a.spr.empty();
}

// Blit the 320×200 arena background into the render target.
// At scale 1: direct std::copy (byte-identical to the original).
// At scale>1: route through the asset cache for per-asset upscaling.
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
// store — never bare-stof them (mirror of game_app.cpp's parse_f; a
// non-numeric value like "loud" would throw through the frame loop).
float parse_f(const std::string& s, float fallback) {
    try { return std::stof(s); } catch (...) { return fallback; }
}

// flow_key_from_sym (the SDL→SettingsFlow::Key bridge) is shared with
// game_app via presentation/dialog_key_map.hpp (CC3) — previously mirrored
// here per OL-B6, now unified without pulling SDL into the pure-logic layer.

}  // namespace

BossRunResult run_boss_level(const std::filesystem::path& game_dir,
                             int internal_level, int lives, long score,
                             const ScaledWindow& bsw,
                             int max_frames, const std::string& shot,
                             int shot_frame, SdlAudio* audio,
                             const BossEnhanceOptions& enhance,
                             const std::string& replay_path,
                             const std::string& trace_path,
                             const std::string& record_inputs_path) {
    BossRunResult res;
    res.lives = lives;
    res.score = score;
    InputReplay replay;
    if (!replay_path.empty() && !replay.load(replay_path)) {
        std::fprintf(stderr,
            "olduvai: --replay '%s' contained no input events — nothing to "
            "replay (is it a --trace capture? record with --record-inputs).\n",
            replay_path.c_str());
    }
    TraceWriter trace;
    if (!trace_path.empty()) trace.open(trace_path);
    InputRecorder input_rec;
    if (!record_inputs_path.empty()) input_rec.open(record_inputs_path);
    BossAssets assets;
    if (!load_boss_assets(game_dir, internal_level, assets)) {
        std::fprintf(stderr, "boss: could not load arena assets\n");
        return res;
    }

    const bool hd = enhance.enhanced && enhance.hd_profile != "native";  // HD ⇔ enhanced
    const int hd_scale = hd ? (enhance.render_scale >= 4 ? 4 : 2) : 1;
    SDL_Window* const win = bsw.win;
    SDL_Renderer* const ren = bsw.ren;
    enhance::HdText hd_text;
    // Vector boss text (HUD labels, tally, loading) belongs to the hd-text /
    // hud-overlay feature cluster.  Only load the font when one is requested;
    // every downstream `hd && hd_text.ok()` guard then falls back to the bitmap
    // path on its own.  --enhanced sets both flags → loads as before.
    if (hd && (enhance.flags.hd_text || enhance.flags.hud_overlay)) {
        std::string base = ".";
        if (char* p = SDL_GetBasePath()) {  // exe dir (non-bundle) or Contents/Resources/ (bundle)
            base = p;
            SDL_free(p);
            if (!base.empty() && base.back() == '/') base.pop_back();
        }
        if (!hd_text.load(base, hd_scale, enhance.hd_font)) {
            enhance::HdText::report_missing(base, enhance.hd_font);
        }
    }
    // Enhanced boss HUD: erase the FULL HUD strip (labels + bar) from assets.bg
    // so blit_bg produces a clean scene; vector labels + bar are recomposed in
    // draw_boss_hud_overlay on every frame.  Capture bar geometry BEFORE erase.
    // bar_left: leftmost green column of the bar in rows 0-5 (scan x >= 268 for
    // the first non-black, non-white pixel after the ENERGY label).
    // bar_color: sampled representative green from the original buffer.
    // erase_pip_column worked at x = health (rows 0-5); bar right = health col.
    int bar_left = 272;  // compile-time default; overwritten by scan below
    // Captured original bar strip: 6 rows × (kBossHealthStart - bar_left + 1) cols,
    // RGBA.  Populated before make_clean_boss_bg erases them; used in
    // draw_boss_hud_overlay for gradient-faithful bar redraw.
    // bar_strip_w: actual width captured (columns bar_left..kBossHealthStart).
    int bar_strip_w = 0;
    std::vector<std::uint8_t> bar_strip_pixels;  // 6 × bar_strip_w × 4 bytes
    if (hd && hd_text.ok() && assets.bg.size() == 320u * 200u * 4u) {
        // Scan row 0 for the leftmost clearly-green column at x >= 268.
        for (int x = 268; x < 318; ++x) {
            const std::size_t off = (static_cast<std::size_t>(0) * 320 + x) * 4;
            const int r = assets.bg[off], g = assets.bg[off + 1], b = assets.bg[off + 2];
            // Green: g dominant and above threshold
            if (g > 60 && g > r && g > b) {
                bar_left = x;
                break;
            }
        }
        // Capture the original bar strip (rows 0-5, cols bar_left..kBossHealthStart)
        // BEFORE make_clean_boss_bg erases those pixels.  The overlay bar sampler
        // reads from this to reproduce the exact baked gradient.
        bar_strip_w = std::max(0, kBossHealthStart - bar_left + 1);
        bar_strip_pixels.resize(static_cast<std::size_t>(6) * bar_strip_w * 4);
        for (int y = 0; y < 6; ++y) {
            for (int x = 0; x < bar_strip_w; ++x) {
                const int src_x = bar_left + x;
                const std::size_t si = (static_cast<std::size_t>(y) * 320 + src_x) * 4;
                const std::size_t di = (static_cast<std::size_t>(y) * bar_strip_w + x) * 4;
                bar_strip_pixels[di]     = assets.bg[si];
                bar_strip_pixels[di + 1] = assets.bg[si + 1];
                bar_strip_pixels[di + 2] = assets.bg[si + 2];
                bar_strip_pixels[di + 3] = assets.bg[si + 3];
            }
        }
        // Replace label-only inpaint with full HUD-strip surgical erase.
        // make_clean_boss_bg removes bright (all-channels>180) HUD pixels —
        // that covers the white LIVES/ENERGY text and the white bar border.
        // The green bar pixels (0,97,32)..(97,194,130) are NOT bright, so
        // explicitly inpaint them too (rows 0-5, bar_left..kBossHealthStart)
        // from the donor row directly below the HUD strip (y + kBossHudStrip),
        // exactly as make_clean_boss_bg does.  This leaves assets.bg
        // scene-clean in the bar region (no black box in the widescreen margin).
        assets.bg = make_clean_boss_bg(assets.bg, 320, 200, kBossHudStrip);
        for (int y = 0; y < 6; ++y) {
            for (int x = bar_left; x <= kBossHealthStart && x < 320; ++x) {
                const std::size_t o = (static_cast<std::size_t>(y) * 320 + x) * 4;
                const std::size_t s =
                    (static_cast<std::size_t>(y + kBossHudStrip) * 320 + x) * 4;
                assets.bg[o]     = assets.bg[s];
                assets.bg[o + 1] = assets.bg[s + 1];
                assets.bg[o + 2] = assets.bg[s + 2];
                assets.bg[o + 3] = 255;
            }
        }
    }
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         320 * hd_scale, 200 * hd_scale);
    // ── Boss-arena widescreen margins (enhanced-only) ─────────────────────────
    // When HD + aspect=="widescreen" and the derived margin M>0, every fight (and
    // victory/KO-flash) frame is presented at (320+2M)×200 native: the HUD-clean
    // RING.PC1 (assets.bg, already inpainted above) is the mirror SOURCE, the
    // margins are a PURE reflection of its edge strips (reflect_pure → black arena
    // walls stay black, no void-scan smear), and the live fight sprites are drawn
    // over the wide buffer at origin_x = M so edge-crossing sprites overflow into
    // the margins.  M==0 (16:10 / 4:3) and classic fall through to the unchanged
    // 320-wide present path below.  Design: boss-arena-widescreen-margins-design.md.
    int ws_ow0 = 0, ws_oh0 = 0;
    SDL_GetRendererOutputSize(ren, &ws_ow0, &ws_oh0);
    const bool ws = hd && enhance.aspect == "widescreen";
    // NON-const: the widescreen margin is recomputed when the renderer output
    // size changes (Alt+Enter fullscreen toggle / resize) — see rebuild_ws_if_
    // resized below.  Without that, the margin is locked to the STARTUP window
    // aspect (created at 320:200 = 16:10 → margin 0), so going fullscreen on a
    // 16:9 display never activated widescreen and just pillarboxed.
    int ws_M = ws ? boss_ws_margin(ws_ow0, ws_oh0,
                                   std::getenv("OLDUVAI_WS_FORCE_MARGIN"))
                  : 0;
    bool ws_active = ws && ws_M > 0;
    int ws_w = 320 + 2 * ws_M;
    SDL_Texture* wtex =
        ws_active ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      ws_w * hd_scale, 200 * hd_scale)
                  : nullptr;
    // HD logical canvas + output-resolution vector-text overlay (see
    // game_app.cpp).  The arena HUD labels are drawn into this overlay at the
    // physical window resolution so they stay crisp at any window scale.
    // Honor the session aspect for the text-overlay logical-size restore
    // (4:3 letterbox / stretch full-bleed) — the window was already created
    // with this aspect; the text overlay's flush must restore it, not the
    // raw 320×200 logical canvas.  "stretch" passes {0,0} (logical scaling
    // disabled) exactly like the surface path.
    // Loading/tally screens stay on the pillarbox path (out of scope for the
    // wide work): start with the aspect_logical fallback so the 320-wide
    // "Please Wait" image is NOT stretched across the wide canvas.  When the
    // wide fight begins (just before the fight loop) the renderer's logical size
    // + these vars switch to the WIDE dims (ws_w*hd_scale × 200*hd_scale) so the
    // wide texture fills the output and the text-overlay flush maps over the full
    // wide canvas — mirroring run_platform_level (game_app.cpp:901-906).  M==0 /
    // non-widescreen keep the fallback for the whole run.
    const LogicalDims _bld = aspect_logical(hd_scale, enhance.aspect);
    int logical_w = _bld.w;
    int logical_h = _bld.h;
    // Recompute widescreen state when the renderer output size changes
    // (Alt+Enter fullscreen toggle / resize).  Cheap no-op when unchanged.
    // MUST update logical_w/logical_h too: the text-overlay flush restores the
    // SDL logical size from those vars, so updating only SDL's logical size
    // gets clobbered back to the stale dims on the next HUD draw — squashing the
    // wide buffer into the old canvas (the fullscreen bug).  OLDUVAI_WS_FORCE_
    // MARGIN pins the margin so this stays a no-op under the test harness.
    auto rebuild_ws_if_resized = [&]() {
        if (!ws) return;
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        if (ow == ws_ow0 && oh == ws_oh0) return;   // unchanged
        ws_ow0 = ow;
        ws_oh0 = oh;
        const int newM =
            boss_ws_margin(ow, oh, std::getenv("OLDUVAI_WS_FORCE_MARGIN"));
        if (newM != ws_M) {
            ws_M = newM;
            ws_w = 320 + 2 * ws_M;
            if (wtex != nullptr) { SDL_DestroyTexture(wtex); wtex = nullptr; }
            if (ws_M > 0)
                wtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         ws_w * hd_scale, 200 * hd_scale);
        }
        ws_active = ws_M > 0;
        // Active → wide canvas fills the output; inactive (margin 0) → the
        // aspect_logical fallback (pillarbox).  Keep the SDL logical size and
        // the restore vars in lockstep.
        logical_w = ws_active ? ws_w * hd_scale : _bld.w;
        logical_h = ws_active ? 200 * hd_scale : _bld.h;
        SDL_RenderSetLogicalSize(ren, logical_w, logical_h);
    };
    TextOverlay text_overlay;

    // In HD mode the arena framebuffer is sized at the target resolution so
    // every compose goes through the per-asset cache path (no whole-frame
    // upscale for fight frames).  Classic stays 320×200.
    const int fb_w = hd ? 320 * hd_scale : 320;
    const int fb_h = hd ? 200 * hd_scale : 200;
    // In-memory per-asset HD upscale cache — cleared at end.  In HD mode it
    // also persists baked blocks under the platform cache dir (stage-2),
    // shared with the platform-level cache; cosmetic + content-addressed, so
    // it never affects gameplay or output.
    enhance::HdAssetCache hd_cache;
    if (hd) hd_cache.enable_disk(prepare::hd_dir());

    // Helper: build a RenderTarget over an arena FrameBuffer.  In HD the
    // target carries the cache and profile so blit_sprite uses the per-asset
    // upscale path; in classic it is a plain scale-1 wrapper.
    auto make_target = [&](FrameBuffer& b) -> RenderTarget {
        if (hd)
            return RenderTarget{b.px.data(), b.w, b.h, hd_scale,
                                &hd_cache, &enhance.hd_profile};
        return RenderTarget{b.px.data(), b.w, b.h, 1, nullptr, nullptr};
    };

    // Level-entry loading screen — same flow as the platform levels
    // (the reference shows "Please Wait" before every level, bosses
    // included); music starts after it, with the fight itself.
    // lpresent is used for loading + score tally screens: these are
    // fullscreen single 320×200 images, so whole-frame upscale is fine
    // (per-asset and whole-frame upscale are identical for a single
    // opaque 320×200 image, and these screens are not arena fight frames).
    auto lpresent = [&](const FrameBuffer& f) -> bool {
        SDL_Event lev;
        while (SDL_PollEvent(&lev)) {
            if (handle_fullscreen_toggle(lev, win)) continue;
            if (lev.type == SDL_QUIT ||
                (lev.type == SDL_KEYDOWN &&
                 lev.key.keysym.sym == SDLK_ESCAPE)) {
                return false;
            }
        }
        if (hd) {
            // Loading/tally screens are always 320×200 — upscale whole-frame.
            const auto up = enhance::upscale_rgba(f.px, 320, 200, hd_scale,
                                                  enhance.hd_profile);
            SDL_UpdateTexture(tex, nullptr, up.data(), 320 * hd_scale * 4);
        } else {
            SDL_UpdateTexture(tex, nullptr, f.px.data(), 320 * 4);
        }
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        SDL_Delay(1000 / 18);
        return true;
    };
    // Enhanced loading screen: cartoon vector font at HD res,
    // same handle shape as the boss tally below.  Classic → null hd_text
    // (byte-identical bitmap path).
    LoadingHd loading_hd;
    if (hd && hd_text.ok()) {
        loading_hd.hd_text = &hd_text;
        loading_hd.scale = hd_scale;
        loading_hd.upscale =
            [&](const std::vector<std::uint8_t>& px) {
                return enhance::upscale_rgba(px, 320, 200, hd_scale,
                                             enhance.hd_profile);
            };
        loading_hd.present_hd =
            [&](const std::vector<std::uint8_t>& hd_px, int w, int h,
                const std::vector<HdTextRow>& rows) -> bool {
                SDL_Event lev;
                while (SDL_PollEvent(&lev)) {
                    if (handle_fullscreen_toggle(lev, win)) continue;
                    if (lev.type == SDL_QUIT ||
                        (lev.type == SDL_KEYDOWN &&
                         lev.key.keysym.sym == SDLK_ESCAPE))
                        return false;
                }
                SDL_UpdateTexture(tex, nullptr, hd_px.data(), w * 4);
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                if (!rows.empty()) {
                    int ow = 0, oh = 0;
                    if (text_overlay.begin(ren, hd_text, ow, oh)) {
                        for (const auto& r : rows) {
                            draw_centered_overlay_row(text_overlay.buffer(),
                                                      ow, oh, hd_text,
                                                      r.native_baseline_y,
                                                      r.text);
                        }
                        text_overlay.flush(ren, logical_w, logical_h);
                    }
                }
                SDL_RenderPresent(ren);
                SDL_Delay(1000 / 18);
                (void)h;
                return true;
            };
    }
    const bool early_quit = !show_loading_screen(
        nullptr, internal_level, assets.charset, assets.palette, lpresent,
        loading_hd);
    if (early_quit) res.quit = true;

    if (!early_quit && audio != nullptr && audio->music_available()) {
        formats::CurArchive fa(slurp(game_dir / "FILESA.CUR"));
        formats::CurArchive fb3(slurp(game_dir / "FILESB.CUR"));
        const std::vector<std::uint8_t>* md = nullptr;
        if (fa.contains("ROCKY.MDI")) md = &fa.get("ROCKY.MDI").data;
        else if (fb3.contains("ROCKY.MDI")) md = &fb3.get("ROCKY.MDI").data;
        if (md != nullptr) {
            audio->play_music(*md, formats::mdi_track_id("rocky.mdi"));
        }
    }
    BossPlayerState player = init_boss_player(lives, score);
    L2BossState l2;
    L4BossState l4;
    L6BossState l6;
    // NO reseed at boss entry: the EXE never re-touches DS:0x87ac after
    // static init, so the LCG state carries over from the preceding level
    // (2026-07-03 review F1 removed a bootstrap-era `!= 2` reseed here).

    // Arena framebuffer: HD-sized when hd, classic 320×200 otherwise.
    FrameBuffer fb{fb_w, fb_h};

    bool running = !early_quit;
    int frame = 0;
    // ── Pause overlay (parity with run_platform_level's ESC menu) ──────
    // Boss fights previously had NO pause: ESC was a bare abort-to-title
    // (2026-07-03 review finding — 3 of 7 play slots lacked the menu and
    // the F5 pipeline).  v1 scope: Resume / Restart Fight / Quit to Title
    // / Quit to Desktop via the shared menus.json `pause_boss` screen.
    // Options/save/cheats stay surface-only (they need the SettingsFlow
    // extraction; saves mid-boss-fight are not a supported concept).
    std::optional<MenuModel> boss_menu_model;
    {
        std::string mbase;
        if (char* mp = SDL_GetBasePath()) { mbase = mp; SDL_free(mp); }
        for (const std::string& cand : {mbase + "data/menus.json",
                                        mbase + "../Resources/data/menus.json",
                                        mbase + "../data/menus.json",
                                        std::string("data/menus.json")}) {
            try { boss_menu_model = load_menus(cand); } catch (...) {
                boss_menu_model.reset();
            }
            if (boss_menu_model) break;
        }
        // No on-disk model — use the compiled-in copy (lone-binary case).
        if (!boss_menu_model) boss_menu_model = load_menus_embedded();
    }
    // OL-B6: real Options bindings — the surface PauseBindings SUBSET that
    // makes sense mid-boss-fight.  Live preview: music/sfx volume (SdlAudio)
    // + fullscreen (SDL window).  enhance.* toggles stage like every other
    // editable key and persist on Apply via the shared
    // encode_enhance_persist encoding (the boss latches its flags at fight
    // entry, so visible effect waits for the next fight).  Reinit-class
    // keys (hd_profile / render_scale / music_device / sfx_backend) and
    // aspect are STAGED + persist-only: boss_app has no reinit machinery
    // (deliberately out of scope) — they apply after the fight (stderr note).
    // Save/Load stay out: mid-boss-fight saves are not a supported concept.
    using BossPersistFn =
        std::function<void(const std::string&, const std::string&)>;
    struct BossPauseBindings : MenuBindings {
        SdlAudio* audio = nullptr;
        SDL_Window* win = nullptr;
        bool enhanced = false;
        const BossPersistFn* persist = nullptr;  // injected app-level config write
        std::map<std::string, std::string> mem;
        SettingsSession* session = nullptr;      // set after construction
        DisplaySettings cur;                     // live baseline at fight entry
        std::string get(const std::string& k) override {
            const auto it = mem.find(k);
            return it == mem.end() ? std::string{} : it->second;
        }
        void save(const std::string& key, const std::string& v) {
            if (persist && *persist) (*persist)(key, v);
        }
        void set(const std::string& k, const std::string& v) override {
            if (k == "preset") {
                // One-click Classic/HD preset: fan the bundle out through
                // this same set() so every key rides the normal machinery.
                mem[k] = v;
                apply_preset(*this, v);
                return;
            }
            // Everything else — enhance.* included — stages provisionally,
            // exactly like the surface pause (persist happens on Apply).
            const std::string old_val = mem.count(k) ? mem[k] : std::string{};
            mem[k] = v;
            // Live preview for the cheap keys (surface-pause parity).
            if (k == "fullscreen" && win) {
                SDL_SetWindowFullscreen(
                    win, v == "1" ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            } else if (k == "music_volume" && audio) {
                audio->set_mix_balance(enhanced, parse_f(v, 100.0f) / 100.0f,
                                       -1.0f);
            } else if (k == "sfx_volume" && audio) {
                audio->set_mix_balance(enhanced, -1.0f,
                                       parse_f(v, 100.0f) / 100.0f);
            }
            // Everything else (hd_profile / render_scale / music_device /
            // sfx_backend / aspect) has NO live path in the boss: staged only.
            if (session) session->stage(k, k, old_val, v);
        }
    } boss_bind;
    MenuModel boss_pause_model = boss_menu_model.value_or(MenuModel{});
    bool pause_open = false;
    // Batched staging: Options edits go through the session → confirm dialog
    // (the same SettingsFlow controller the surface pause uses; OL-B1/OL-B6).
    SettingsSession boss_session;
    ConfirmDialog boss_confirm;
    boss_bind.audio = audio;
    boss_bind.win = win;
    boss_bind.enhanced = enhance.enhanced;
    boss_bind.persist = &enhance.persist;
    boss_bind.session = &boss_session;
    boss_bind.cur = {enhance.enhanced,
                     enhance.hd_profile.empty() ? "native" : enhance.hd_profile,
                     enhance.render_scale, enhance.music_device,
                     enhance.sfx_backend};
    boss_bind.mem["music_device"] = enhance.music_device;
    boss_bind.mem["sfx_backend"] = enhance.sfx_backend;
    boss_bind.mem["hd_profile"] =
        enhance.hd_profile.empty() ? "native" : enhance.hd_profile;
    boss_bind.mem["render_scale"] = std::to_string(enhance.render_scale);
    boss_bind.mem["aspect"] = enhance.aspect.empty() ? "keep" : enhance.aspect;
    // Master-flag baseline: lets a preset click that matches the current
    // style net out of the staging diff (encode_enhance_persist also keys
    // off whether the master genuinely changed).
    boss_bind.mem["enhanced"] = enhance.enhanced ? "true" : "false";
    boss_bind.mem["preset"] =
        !boss_bind.enhanced ? "dos"
                            : (boss_bind.mem["aspect"] == "4:3" ? "hd-43"
                                                                : "hd");
    boss_bind.mem["fullscreen"] =
        (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? "1" : "0";
    boss_bind.mem["music_volume"] = "100";
    boss_bind.mem["sfx_volume"] = "100";
    boss_bind.mem["enhance.smooth_motion"] =
        enhance.flags.smooth_motion ? "1" : "0";
    boss_bind.mem["enhance.hd_text"] = enhance.flags.hd_text ? "1" : "0";
    boss_bind.mem["enhance.hud_overlay"] =
        enhance.flags.hud_overlay ? "1" : "0";
    boss_bind.mem["enhance.cinematic_cue"] =
        enhance.flags.cinematic_cue ? "1" : "0";
    boss_bind.mem["enhance.fluid_bubbles"] =
        enhance.flags.fluid_bubbles ? "1" : "0";
    boss_bind.mem["enhance.secret_slide"] =
        enhance.flags.secret_slide ? "1" : "0";
    boss_bind.mem["enhance.descent_pan"] =
        enhance.flags.descent_pan ? "1" : "0";
    MenuActionTable boss_pause_actions = {
        {"resume", [&] { pause_open = false; }},
        {"restart_level", [&] { res.restart = true; running = false; }},
        {"quit_title", [&] { res.quit = true; running = false; }},
        {"quit_desktop", [&] {
            res.quit = true;
            res.quit_program = true;
            running = false;
        }},
    };
    Menu boss_pause_menu(boss_pause_model, boss_bind, boss_pause_actions);
    // SettingsFlow (OL-B6): the SAME controller the surface pause and main
    // menu use (OL-B1) — subtree-exit detection, confirm-dialog keys,
    // apply/discard resolution.  Boss-specific effects live in these hooks;
    // there is NO reinit path here — reinit-class keys persist with a note.
    SettingsFlow::Hooks boss_hooks;
    boss_hooks.persist = [&](const std::string& k, const std::string& v) {
        // enhance.* keys persist as ONE "enhance" config list (plus the
        // master companion when the master is not staged) — see
        // encode_enhance_persist.
        if (k.rfind("enhance.", 0) == 0) {
            for (const auto& [pk, pv] : encode_enhance_persist(
                     boss_bind.mem, boss_session.changes(), k))
                boss_bind.save(pk, pv);
            return;
        }
        boss_bind.save(k, v);
    };
    boss_hooks.classify = [&](const std::string& k, const std::string& v) {
        return classify_change(k, v, boss_bind.cur);
    };
    boss_hooks.apply_change = [&](const StagedChange& ch, ApplyTier tier) {
        // Volumes/fullscreen already previewed live.  Anything the boss
        // cannot honor in place (display/audio pipeline + aspect) is
        // persist-only in v1 — it applies after the fight.
        (void)tier;
        const bool deferred =
            ch.key == "hd_profile" || ch.key == "render_scale" ||
            ch.key == "music_device" || ch.key == "sfx_backend" ||
            ch.key == "aspect";
        if (deferred)
            std::fprintf(stderr,
                         "boss options: %s=%s saved — applies after the "
                         "fight\n",
                         ch.key.c_str(), ch.new_value.c_str());
    };
    boss_hooks.revert_change = [&](const StagedChange& ch) {
        boss_bind.mem[ch.key] = ch.old_value;
        // Re-apply the cheap live preview at baseline.
        if (ch.key == "fullscreen" && boss_bind.win) {
            SDL_SetWindowFullscreen(boss_bind.win,
                                    ch.old_value == "1"
                                        ? SDL_WINDOW_FULLSCREEN_DESKTOP
                                        : 0);
        } else if (ch.key == "music_volume" && boss_bind.audio) {
            boss_bind.audio->set_mix_balance(
                boss_bind.enhanced, parse_f(ch.old_value, 100.0f) / 100.0f,
                -1.0f);
        } else if (ch.key == "sfx_volume" && boss_bind.audio) {
            boss_bind.audio->set_mix_balance(
                boss_bind.enhanced, -1.0f,
                parse_f(ch.old_value, 100.0f) / 100.0f);
        }
    };
    boss_hooks.reopen_options = [&]() { boss_pause_menu.open("options"); };
    boss_hooks.confirm_note = [](bool any_reinit, bool any_persist) {
        if (any_reinit) return std::string("Applies after the fight.");
        if (any_persist)
            return std::string("Saved - takes effect on next launch.");
        return std::string{};
    };
    SettingsFlow boss_flow(boss_pause_model, boss_session, boss_confirm,
                           std::move(boss_hooks));
    // Close-without-apply detection (surface parity): pause closed via
    // Resume/ESC while the session is dirty = implicit Discard.
    bool was_pause_open = false;
    SDL_Texture* pause_tex = nullptr;   // native 320x200 pause frame (lazy)
    const bool boss_menu_ok =
        boss_menu_model.has_value() &&
        boss_pause_model.screens.count("pause_boss") != 0;
    bool bug_capture_pending = false;   // F5 — written after the next render
    const Uint32 frame_ms = 1000 / 18;   // aux pacing sites
    DosTicker dos_ticker;                // drift-free 18.2065 Hz main pacing
    bool vga_scan_ok = true;             // cleared when vsync clearly refused
    // Smooth motion: vsync-locked render interpolation (general technique —
    // see smooth_present.hpp); falls back to discrete sub-frames without vsync.
    const bool smooth =
        enhance.flags.smooth_motion && max_frames <= 0 && shot.empty();
    SmoothPacer pacer;
    pacer.frame_ms = frame_ms;
    pacer.discrete_n = smooth_subframe_count(win);
    pacer.vsync = smooth_try_enable_vsync(ren, smooth);
    bool smooth_vsync_ran = false;   // set per-frame; skips the outer delay
    // Part 1 follow-up: the boss player's float render position, carried into
    // render_fight's rt + present_wide's overflow wrt so render_boss_player_fb
    // draws it sub-pixel on the HD smooth sub-frame.  Set by the smooth loop;
    // false elsewhere → integer path (byte-identical).
    bool boss_use_float = false;
    float boss_pfx = 0.0f, boss_pfy = 0.0f;
    // F4 — present_frame(draw_lives): boss HUD label draw.
    // Classic (non-hd_text) path: bitmap 2-digit lives at (48,8) unchanged.
    // Enhanced path: erase was done once at load (assets.bg patched above);
    //   now draw vector "LIVES:" at native (0,8), "ENERGY" at native (219,8),
    //   and the 2-digit value at native (48,8) when draw_lives is true.
    //   draw_lives=false is the L2 victory flash where the EXE never redraws
    //   the lives digit — but the labels ARE drawn per the reference.
    //   All coords × hd_scale for the HD RGBA buffer (baseline_y = 8*hd_scale).
    // In HD mode, fb is already at HD resolution — no upscale needed; draw
    // vector HUD directly on fb.px then upload at fb.w pitch.
    // Draw the boss HUD vector labels into the output-resolution overlay:
    // "LIVES:" at native (0,8), "ENERGY" at native (219,8), and the 2-digit
    // lives value at native (48,8) when draw_lives.  The pip bar lives over the
    // center 320 sub-region (drained-column gradient sample).
    //
    // ONE implementation for both the non-widescreen path AND the widescreen
    // path.  `cx_native` = the native x of the center column (0 for the
    // 320-wide / pillarbox path, ws_M for the wide path); `total_native_w` =
    // the full native width of the composed buffer (320 for the 320-wide path,
    // 320+2*ws_M for the wide path).  Native HUD x is mapped to the wide domain
    // as (cx_native + x), then to output by sx = ow/total_native_w.  With the
    // defaults (cx_native=0, total_native_w=320) sx = ow/320 and there is no x
    // shift — byte-identical to the prior 320-only overlay.  The vertical scale
    // sy = oh/200 is unchanged in both domains (height is always 200 native).
    // Buffer-agnostic HUD draw: labels + energy bar into an arbitrary RGBA
    // buffer `buf` of size (bw × bh).  `bw`/`bh` are the OUTPUT-resolution
    // dimensions of the buffer (renderer output size for the live overlay;
    // (ws_w*hd_scale × 200*hd_scale) for the offscreen wide screenshot).  All
    // mapping math (sx, sy, ox) is identical to the prior overlay — only the
    // destination buffer + its dims differ.
    auto draw_boss_hud_into = [&](std::vector<std::uint8_t>& buf, int bw,
                                  int bh, bool draw_lives, int cx_native,
                                  int total_native_w) {
        const double sx = bw / static_cast<double>(total_native_w);
        const double sy = bh / 200.0;
        const int base_y = static_cast<int>(8 * sy + 0.5);
        // Pin the font cap to the centre-320 metric (8 native px × sx).  Without
        // this the HUD inherits the overlay's default cap, which is sized by the
        // full OUTPUT width ÷ 320 — correct at 320, but OVER-scaled in widescreen
        // where the canvas (and output) is ws_native_w wide, not 320.  That made
        // the wide HUD font too big and out of proportion with the bar (which is
        // sized via sx).  saved_cap restored at function end.
        const int saved_cap = hd_text.cap_px();
        hd_text.set_cap_px(std::max(1, static_cast<int>(8.0 * sx + 0.5)));
        // native x → output x in the wide domain: (cx_native + x) * sx.
        auto ox = [&](int nx) {
            return static_cast<int>((cx_native + nx) * sx + 0.5);
        };
        // §5.2a: compose LIVES as a single string so the HD proportional font
        // never overprints the label with the digit field.  ENERGY shifted left
        // to ox(150) so it clears the energy bar at all aspect ratios.
        if (draw_lives) {
            char vbuf[16];
            std::snprintf(vbuf, sizeof vbuf, "LIVES: %02d", std::max(0, player.lives));
            hd_text.draw(buf, bw, bh, ox(0), base_y, vbuf, 235, 235, 235);
        } else {
            hd_text.draw(buf, bw, bh, ox(0), base_y, "LIVES:", 235, 235, 235);
        }
        // ── Energy gauge: white-framed bar matching the surface FOOD/energy
        // gauge style (1px white border + dark drained interior), with the boss
        // bar's OWN captured gradient inside (bar_strip_pixels, colours kept).
        // The ENERGY label is right-aligned just left of the frame instead of
        // floating at x=150 far from the bar.  bar native extent =
        // [bar_left .. kBossHealthStart] rows 0-5; the framed box is rows 0-7
        // with the gradient on the inner rows 1-6.  Colours match enhanced_hud's
        // kWhite{235,235,235} / kEmpty{18,18,30}.
        auto fill_nat = [&](int nx0, int ny0, int nx1, int ny1,
                            int cr, int cg, int cb) {
            const int oxa = ox(nx0);
            const int oxb = ox(nx1 + 1);
            const int oya = static_cast<int>(ny0 * sy + 0.5);
            const int oyb = static_cast<int>((ny1 + 1) * sy + 0.5);
            for (int oy = oya; oy < oyb && oy < bh; ++oy)
                for (int oxx = oxa; oxx < oxb && oxx < bw; ++oxx) {
                    const std::size_t oi =
                        (static_cast<std::size_t>(oy) * bw + oxx) * 4;
                    buf[oi] = cr; buf[oi + 1] = cg; buf[oi + 2] = cb;
                    buf[oi + 3] = 255;
                }
        };
        if (!bar_strip_pixels.empty() && bar_strip_w > 0) {
            const int cur_health = internal_level == 2 ? l2.health
                                 : internal_level == 4 ? l4.health
                                                       : l6.health;
            const int health_col = std::min(cur_health, kBossHealthStart);
            const int fx0 = bar_left - 1, fx1 = kBossHealthStart + 1;
            const int fy0 = 0, fy1 = 7;
            // Dark drained interior across the whole inner area first.
            fill_nat(bar_left, 1, kBossHealthStart, 6, 18, 18, 30);
            // Gradient fill [bar_left..health_col], native rows 1-6 ← strip 0-5
            // (preserves the captured boss-energy colours).
            for (int nrow = 1; nrow <= 6; ++nrow) {
                const int srow = nrow - 1;
                const int oya = static_cast<int>(nrow * sy + 0.5);
                const int oyb = static_cast<int>((nrow + 1) * sy + 0.5);
                for (int oy = oya; oy < oyb && oy < bh; ++oy)
                    for (int ncol = bar_left; ncol <= health_col; ++ncol) {
                        const int scol =
                            std::min(ncol - bar_left, bar_strip_w - 1);
                        const std::size_t si =
                            (static_cast<std::size_t>(srow) * bar_strip_w + scol) * 4;
                        const int oxa = ox(ncol), oxb = ox(ncol + 1);
                        for (int oxx = oxa; oxx < oxb && oxx < bw; ++oxx) {
                            const std::size_t oi =
                                (static_cast<std::size_t>(oy) * bw + oxx) * 4;
                            buf[oi] = bar_strip_pixels[si];
                            buf[oi + 1] = bar_strip_pixels[si + 1];
                            buf[oi + 2] = bar_strip_pixels[si + 2];
                            buf[oi + 3] = 255;
                        }
                    }
            }
            // White 1px frame (4 edges).
            fill_nat(fx0, fy0, fx1, fy0, 235, 235, 235);   // top
            fill_nat(fx0, fy1, fx1, fy1, 235, 235, 235);   // bottom
            fill_nat(fx0, fy0, fx0, fy1, 235, 235, 235);   // left
            fill_nat(fx1, fy0, fx1, fy1, 235, 235, 235);   // right
            // ENERGY label right-aligned just left of the frame.
            const int gap = static_cast<int>(4 * sx + 0.5);
            const int label_right = ox(fx0) - gap;
            const int label_w = hd_text.measure("ENERGY");
            hd_text.draw(buf, bw, bh, label_right - label_w, base_y, "ENERGY",
                         235, 235, 235);
        } else {
            hd_text.draw(buf, bw, bh, ox(150), base_y, "ENERGY", 235, 235, 235);
        }
        hd_text.set_cap_px(saved_cap);   // restore for any later text in the pass
    };
    // Live overlay path: draw the HUD into the renderer's output-resolution
    // text-overlay buffer, then flush it over the scene.
    auto draw_boss_hud_overlay_at = [&](bool draw_lives, int cx_native,
                                        int total_native_w) {
        int ow = 0, oh = 0;
        if (!text_overlay.begin(ren, hd_text, ow, oh)) return;
        draw_boss_hud_into(text_overlay.buffer(), ow, oh, draw_lives, cx_native,
                           total_native_w);
        text_overlay.flush(ren, logical_w, logical_h);
    };
    // Non-widescreen overlay: center origin 0, total native width 320.  This
    // reproduces the prior draw_boss_hud_overlay output exactly.
    auto draw_boss_hud_overlay = [&](bool draw_lives) {
        draw_boss_hud_overlay_at(draw_lives, /*cx_native=*/0,
                                 /*total_native_w=*/320);
    };
    // Widescreen overlay: center origin ws_M, total native width ws_w (=320+2M).
    auto draw_boss_hud_overlay_wide = [&](bool draw_lives) {
        draw_boss_hud_overlay_at(draw_lives, /*cx_native=*/ws_M,
                                 /*total_native_w=*/ws_w);
    };
    auto present_frame = [&](bool draw_lives = true, bool do_present = true) {
        rebuild_ws_if_resized();
        // Bitmap path: unchanged (classic only, draw into 320×200 fb).
        if (!hd || !hd_text.ok()) {
            if (draw_lives) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "%02d",
                              std::max(0, player.lives));
                draw_text(fb, assets.charset, assets.palette, 48, 8, buf);
            }
        }
        if (hd) {
            // Arena fb is already HD — upload directly (no upscale, no text).
            SDL_UpdateTexture(tex, nullptr, fb.px.data(), fb.w * 4);
        } else {
            SDL_UpdateTexture(tex, nullptr, fb.px.data(), 320 * 4);
        }
        SDL_RenderClear(ren);
        if (ws_active) {
            // Wide logical canvas active but this is a non-wide present (the
            // victory/KO-flash fallback): pillarbox the 320 texture into the
            // centered dst rect so it keeps its 4:3-ish proportions inside the
            // wide canvas (no horizontal stretch), then draw the HUD with the
            // WIDE mapping so labels/bar sit over the centered 320 region.
            const SDL_Rect dst{ws_M * hd_scale, 0, 320 * hd_scale,
                               200 * hd_scale};
            SDL_RenderCopy(ren, tex, nullptr, &dst);
            if (hd && hd_text.ok()) draw_boss_hud_overlay_wide(draw_lives);
        } else {
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            // Vector HUD labels at OUTPUT resolution (crisp at any window scale).
            if (hd && hd_text.ok()) draw_boss_hud_overlay(draw_lives);
        }
        if (do_present) SDL_RenderPresent(ren);
    };
    // ── Widescreen present (enhanced + aspect=="widescreen" + M>0) ────────────
    // Build the wide upscaled scene buffer for ONE fight frame: native compose
    // (HUD-clean center, mirror margins) → sprite overflow at origin_x=M → whole-
    // frame upscale.  Returns the (ws_w*hd_scale × 200*hd_scale) RGBA buffer used
    // by present_wide AND the wide screenshot branch (identical pixels).
    // Phase-2 perf (task #61): the boss arena background (RING.PC1, a single
    // STATIC screen) never changes during the fight — only the boss/player
    // sprites move.  So cache its composed+upscaled WIDE form (centre +
    // mirror margins + edge gradient) and per frame just copy it and draw the
    // dynamic sprites at HD via the per-asset cache — no per-frame whole-frame
    // upscale (omniscale was the budget-buster, same as the surface path).
    // Rebuilt only on margin (Alt+Enter) / hd-profile change.
    std::vector<std::uint8_t> boss_bg_hd;
    int boss_bg_hd_M = -1;
    std::string boss_bg_hd_profile;
    auto build_wide_up = [&]() -> std::vector<std::uint8_t> {
        // 1. Cached static wide bg: HUD-clean assets.bg composed wide (margins =
        //    pure reflection of its edge strips, reflect_pure=true so the black
        //    arena walls stay black; 0.10 edge-darkening gradient), upscaled ONCE.
        if (boss_bg_hd_M != ws_M || boss_bg_hd_profile != enhance.hd_profile) {
            FrameBuffer cbg{320, 200};
            std::copy(assets.bg.begin(), assets.bg.end(), cbg.px.begin());
            std::vector<std::uint8_t> wide;
            compose_widescreen(wide, ws_M, cbg, /*left=*/nullptr, /*right=*/nullptr,
                               /*hud_rows=*/0, /*backdrop=*/nullptr,
                               /*reflect_pure=*/true,
                               /*margin_edge_brightness=*/0.10f);
            boss_bg_hd = enhance::upscale_rgba(wide, ws_w, 200, hd_scale,
                                               enhance.hd_profile);
            boss_bg_hd_M = ws_M;
            boss_bg_hd_profile = enhance.hd_profile;
        }
        std::vector<std::uint8_t> out = boss_bg_hd;   // copy cached HD wide bg
        // 2. draw the live fight sprites over the HD buffer at origin_x = M (per-
        //    asset HD cache) so edge-crossing sprites overflow into the margins.
        //    advance_state=false (purely visual — no gameplay state mutation).
        RenderTarget wrt{out.data(), ws_w * hd_scale, 200 * hd_scale, hd_scale,
                         &hd_cache, &enhance.hd_profile};
        wrt.origin_x = ws_M;
        wrt.advance_state = false;
        wrt.use_float_pos = boss_use_float;
        wrt.player_fx = boss_pfx;
        wrt.player_fy = boss_pfy;
        if (internal_level == 2)      render_l2_sprites(wrt, assets, player, l2);
        else if (internal_level == 4) render_l4_sprites(wrt, assets, player, l4);
        else                          render_l6_sprites(wrt, assets, player, l6);
        return out;
    };
    auto present_wide = [&](bool draw_lives = true, bool do_present = true) {
        rebuild_ws_if_resized();
        // A resize this frame may have dropped widescreen (margin → 0, wtex
        // freed): fall back to the pillarbox present rather than touch a null
        // texture.  present_frame is defined above.
        if (!ws_active || wtex == nullptr) {
            present_frame(draw_lives, do_present);
            return;
        }
        const std::vector<std::uint8_t> up = build_wide_up();
        SDL_UpdateTexture(wtex, nullptr, up.data(), ws_w * hd_scale * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, wtex, nullptr, nullptr);
        // Vector HUD over the center 320 sub-region (mapped into the wide domain).
        if (hd_text.ok()) draw_boss_hud_overlay_wide(draw_lives);
        if (do_present) SDL_RenderPresent(ren);
    };
    // Present a NATIVE 320×200 frame (a victory sequence frame) at the WIDE
    // width: wrap it with the same darkened-mirror margins as the fight
    // (compose_widescreen reflect_pure + the 0.10 edge gradient), upscale,
    // present, and draw the wide HUD — so victory matches the fight's
    // widescreen look instead of pillarboxing.  Defensive 320 fallback if a
    // resize dropped widescreen mid-sequence.
    // Last native frame sent through the wide present — reused as the source
    // for the post-victory wide fade (the HD `fb` holds the stale fight frame).
    FrameBuffer last_wide_nat;
    int l2_last_flash = 0;   // last L2 victory flash frame — fade source parity
    auto present_wide_native = [&](const FrameBuffer& nat,
                                   bool draw_lives = true,
                                   bool do_present = true,
                                   bool draw_hud = true) {
        rebuild_ws_if_resized();
        last_wide_nat = nat;
        if (!ws_active || wtex == nullptr) {
            std::vector<std::uint8_t> up = enhance::upscale_rgba(
                nat.px, 320, 200, hd_scale, enhance.hd_profile);
            SDL_UpdateTexture(tex, nullptr, up.data(), 320 * hd_scale * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            if (draw_hud && hd && hd_text.ok()) draw_boss_hud_overlay(draw_lives);
            if (do_present) SDL_RenderPresent(ren);
            return;
        }
        std::vector<std::uint8_t> wide;
        compose_widescreen(wide, ws_M, nat, /*left=*/nullptr, /*right=*/nullptr,
                           /*hud_rows=*/0, /*backdrop=*/nullptr,
                           /*reflect_pure=*/true,
                           /*margin_edge_brightness=*/0.10f);
        std::vector<std::uint8_t> up =
            enhance::upscale_rgba(wide, ws_w, 200, hd_scale, enhance.hd_profile);
        SDL_UpdateTexture(wtex, nullptr, up.data(), ws_w * hd_scale * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, wtex, nullptr, nullptr);
        if (draw_hud && hd_text.ok()) draw_boss_hud_overlay_wide(draw_lives);
        if (do_present) SDL_RenderPresent(ren);
    };
    // Route a FIGHT present through the wide path when active, else the
    // unchanged 320-wide present.  Scope note: steady fight frames AND the L4
    // ride-off victory are routed wide.  L4 is special: the dino + player move
    // HORIZONTALLY to the screen edge, so mirroring a baked 320 frame
    // (present_wide_native) reflected + clipped them — instead it gets the SAME
    // clean-bg-wide + sprite-overflow treatment as the fight (render_l4_victory_
    // sprites at origin_x = ws_M; see the l4_victory branch).  L2 (static
    // defeated-quad flash) and L6 (vertical drop) victories DON'T move
    // horizontally, so present_wide_native's mirror is correct for them — they
    // keep it.  The fade + score-tally that follow run pillarboxed (out of scope
    // here).  Screenshots only exercise the fight path (victory runs only when
    // shot.empty()), so the wide screenshot branch is unaffected.
    auto present_any = [&](bool draw_lives = true) {
        const bool l4_victory = ws_active && internal_level == 4 && l4.win_flag >= 1;
        if (l4_victory) {
            // L4 ride-off victory: build the wide buffer the SAME way the fight
            // does (build_wide_up) — clean arena bg composed wide (mirror + 0.10
            // edge gradient), then the victory SPRITES drawn ONCE at origin_x =
            // ws_M so the riding triceratops + player OVERFLOW into the margins.
            // The old path baked the sprites into a 320 frame and then mirrored
            // it (present_wide_native), which duplicated + clipped the ride-off
            // dino at the screen edges.  Defensive 320 fallback if a resize
            // dropped widescreen mid-ride.
            rebuild_ws_if_resized();
            // Keep the post-victory wide fade source (last_wide_nat) current: it
            // is faded native-wrapped after the loop; by then the dino has ridden
            // off so the baked-native frame is a fine (near-empty) fade source.
            FrameBuffer vnat;
            {
                RenderTarget nrt{vnat.px.data(), 320, 200, 1, nullptr, nullptr};
                render_l4_victory_frame(nrt, assets, player, l4);
            }
            last_wide_nat = vnat;
            if (!ws_active || wtex == nullptr) {   // resize dropped widescreen
                present_wide_native(vnat, draw_lives);
                return;
            }
            FrameBuffer cbg{320, 200};
            std::copy(assets.bg.begin(), assets.bg.end(), cbg.px.begin());
            std::vector<std::uint8_t> wide;
            compose_widescreen(wide, ws_M, cbg, /*left=*/nullptr, /*right=*/nullptr,
                               /*hud_rows=*/0, /*backdrop=*/nullptr,
                               /*reflect_pure=*/true,
                               /*margin_edge_brightness=*/0.10f);
            RenderTarget wrt{wide.data(), ws_w, 200, 1, nullptr, nullptr};
            wrt.origin_x = ws_M;
            wrt.advance_state = false;
            render_l4_victory_sprites(wrt, assets, player, l4);
            std::vector<std::uint8_t> up = enhance::upscale_rgba(
                wide, ws_w, 200, hd_scale, enhance.hd_profile);
            SDL_UpdateTexture(wtex, nullptr, up.data(), ws_w * hd_scale * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, wtex, nullptr, nullptr);
            if (hd_text.ok()) draw_boss_hud_overlay_wide(draw_lives);
            SDL_RenderPresent(ren);
        } else if (ws_active) {
            present_wide(draw_lives);
        } else {
            present_frame(draw_lives);
        }
    };
    // Switch the renderer to the WIDE logical canvas now that loading is done —
    // the fight (and the victory/tally that fall back to present_frame) all run
    // below.  present_frame's 320-wide tex is RenderCopy'd to the whole output,
    // so under the wide logical size it pillarboxes the 320 center inside the
    // wide canvas — matching the intended "victory/tally pillarbox" look.
    if (ws_active) {
        SDL_RenderSetLogicalSize(ren, ws_w * hd_scale, 200 * hd_scale);
        logical_w = ws_w * hd_scale;
        logical_h = 200 * hd_scale;
    }
    while (running) {
        const Uint32 t0 = SDL_GetTicks();
        cursor_autohide_frame();   // keyboard game: park the OS arrow
        // Detect pause closing (Resume/ESC) with a dirty session → implicit
        // Discard (§8.6 step 4; APPLY clears the session, so no double-revert).
        if (was_pause_open && !pause_open && !boss_session.empty())
            boss_flow.discard();
        was_pause_open = pause_open;
        smooth_vsync_ran = false;   // set true only when the vsync fill paced
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (handle_fullscreen_toggle(ev, win)) continue;
            if (ev.type == SDL_QUIT) {
                res.quit = true;
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                const SDL_Keycode sym = ev.key.keysym.sym;
                if (pause_open) {
                    // Confirm dialog intercepts all input while open (§8.6
                    // step 4).  SettingsFlow resolves move/apply/discard/
                    // cancel through the boss hooks above (OL-B6).
                    if (boss_confirm.is_open()) {
                        boss_flow.handle_key(flow_key_from_sym(sym));
                    } else if (sym == SDLK_ESCAPE) {
                        // Back out one screen (Options → pause root), close
                        // at the root — same idiom as the surface pause.
                        boss_pause_menu.back();
                        if (!boss_pause_menu.is_open()) pause_open = false;
                    } else if (sym == SDLK_UP || sym == SDLK_w) {
                        boss_pause_menu.move(-1);
                    } else if (sym == SDLK_DOWN || sym == SDLK_s) {
                        boss_pause_menu.move(+1);
                    } else if (sym == SDLK_LEFT || sym == SDLK_a) {
                        boss_pause_menu.adjust(-1);
                    } else if (sym == SDLK_RIGHT || sym == SDLK_d) {
                        boss_pause_menu.adjust(+1);
                    } else if (sym == SDLK_RETURN || sym == SDLK_SPACE) {
                        boss_pause_menu.activate();
                    }
                } else if (sym == SDLK_ESCAPE) {
                    // ESC opens the pause overlay (menus.json present);
                    // replay sessions and the no-menu fallback keep the
                    // old bare abort-to-title.
                    if (boss_menu_ok && !replay.active()) {
                        pause_open = true;
                        boss_pause_menu.open("pause_boss");
                    } else {
                        res.quit = true;
                        running = false;
                    }
                } else if (sym == SDLK_F5) {
                    bug_capture_pending = true;   // written after the render
                }
            }
        }
        // Debug: OLDUVAI_BOSS_PAUSE_SHOT=<path.bmp> force-opens the pause
        // overlay after the fly-in and dumps one frame (headless check,
        // mirrors game_app's OLDUVAI_PAUSE_SHOT).
        static const char* const s_bps = std::getenv("OLDUVAI_BOSS_PAUSE_SHOT");
        if (s_bps && frame == 60 && boss_menu_ok && !pause_open) {
            // OLDUVAI_BOSS_PAUSE_SCREEN picks the screen to capture (default
            // "pause_boss") — lets the headless render check reach the shared
            // Options screens (mirrors game_app's OLDUVAI_PAUSE_SCREEN).
            const char* ps = std::getenv("OLDUVAI_BOSS_PAUSE_SCREEN");
            boss_pause_menu.open(ps != nullptr ? ps : "pause_boss");
            pause_open = boss_pause_menu.is_open();  // unknown id → no overlay
        }
        // Options-exit detection (§8.6 step 2): after input handling,
        // SettingsFlow checks whether the menu just left the Options subtree
        // (membership derived from menus.json) and opens the confirm dialog
        // if changes are staged.
        if (pause_open && boss_pause_menu.is_open() && !boss_confirm.is_open())
            boss_flow.track_screen(boss_pause_menu.current_screen());
        // Pause freeze: draw the frozen fight + menu, skip logic/frame++.
        // The fight fb is HD-SIZED in HD mode, but the menu layout is
        // native-coordinate — render the frozen scene at native 320×200
        // into a dedicated buffer/texture and pillarbox it, exactly like
        // present_frame's non-wide fallback.  Native-res during pause is
        // fine (the menu IS the focus); the HD frame returns on resume.
        if (pause_open && running) {
            FrameBuffer pf{320, 200};
            RenderTarget prt{pf.px.data(), 320, 200, 1, nullptr, nullptr};
            if (internal_level == 2) render_l2_frame(prt, assets, player, l2);
            else if (internal_level == 4) render_l4_frame(prt, assets, player, l4);
            else render_l6_frame(prt, assets, player, l6);
            const formats::Sprite* bone =
                assets.bone_atlas.size() > 33 ? &assets.bone_atlas[33] : nullptr;
            // HD hybrid font (same gate as game_app's surface pause): the slab
            // + dim come from draw_menu (native, upscaled with the frame); the
            // glyphs + bone cursor move to the output-resolution vector
            // overlay (draw_menu_vector) so they stay crisp.  Classic keeps
            // the bitmap glyphs drawn into the native pause buffer.
            const bool menu_use_vector =
                hd && hd_text.ok() &&
                (enhance.flags.hd_text || enhance.flags.hud_overlay);
            if (boss_confirm.is_open()) {
                // Confirm dialog replaces the menu while open (§8.6 step 5)
                // — same draw_confirm reuse as game_app's surface pause.
                draw_confirm(pf, boss_confirm, assets.charset, /*dim=*/true,
                             /*draw_text=*/!menu_use_vector);
            } else {
                draw_menu(pf, boss_pause_menu, assets.charset, /*dim=*/true,
                          /*draw_text=*/!menu_use_vector, bone,
                          bone != nullptr ? &assets.bone_palette : nullptr);
            }
            if (pause_tex == nullptr)
                pause_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              320, 200);
            if (pause_tex != nullptr) {
                SDL_UpdateTexture(pause_tex, nullptr, pf.px.data(), 320 * 4);
                SDL_RenderClear(ren);
                if (ws_active) {
                    const SDL_Rect dst{ws_M * hd_scale, 0, 320 * hd_scale,
                                       200 * hd_scale};
                    SDL_RenderCopy(ren, pause_tex, nullptr, &dst);
                } else {
                    SDL_RenderCopy(ren, pause_tex, nullptr, nullptr);
                }
            }
            if (menu_use_vector) {
                int ow = 0, oh = 0;
                if (text_overlay.begin(ren, hd_text, ow, oh)) {
                    // Widescreen: the pause frame is PILLARBOXED at ws_M (the
                    // dst rect above) — pass that frame rect so the glyphs
                    // land on the slab instead of stretching across the bars.
                    // Non-WS: the frame fills the canvas (defaults of -1).
                    int mfx = -1, mfy = -1, mfw = -1, mfh = -1;
                    if (ws_active) {
                        mfx = ws_M * hd_scale * ow / logical_w;
                        mfw = 320 * hd_scale * ow / logical_w;
                        mfy = 0;
                        mfh = oh;
                    }
                    if (boss_confirm.is_open())
                        draw_confirm_vector(text_overlay.buffer(), ow, oh,
                                            hd_text, boss_confirm, mfx, mfy,
                                            mfw, mfh);
                    else
                        draw_menu_vector(text_overlay.buffer(), ow, oh,
                                         hd_text, boss_pause_menu, 0.0f, mfx,
                                         mfy, mfw, mfh);
                    text_overlay.flush(ren, logical_w, logical_h);
                }
            }
            if (s_bps) {                     // headless: dump + exit
                // Readback BEFORE the buffer swap (post-present readback is
                // black on Metal — same rule as the --shot path).
                int ow = 0, oh = 0;
                SDL_GetRendererOutputSize(ren, &ow, &oh);
                SDL_Surface* sfc = SDL_CreateRGBSurfaceWithFormat(
                    0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
                if (sfc != nullptr &&
                    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                         sfc->pixels, sfc->pitch) == 0)
                    save_surface_image(sfc, s_bps);
                if (sfc != nullptr) SDL_FreeSurface(sfc);
                res.quit = true;
                running = false;
                continue;
            }
            SDL_RenderPresent(ren);
            SDL_Delay(16);
            continue;
        }
        // Debug: OLDUVAI_AUTO_FULLSCREEN=<frame> programmatically toggles
        // desktop-fullscreen at that frame (simulates Alt+Enter) so the
        // recompute path can be captured deterministically.
        if (const char* afs = std::getenv("OLDUVAI_AUTO_FULLSCREEN")) {
            if (frame == std::atoi(afs))
                SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
        BossInputs in;
        if (replay.active()) {
            // Reference boss convention: frame N's logic uses key state
            // folded up to frame N, and frame 0 always runs input-free
            // (the reference engine's boss-replay key-state convention).  This
            // differs from run_platform_level's at(frame + 1): there the
            // two engines' surface traces sit one line apart and the
            // differ re-aligns them, but the boss fly-in (44 identical
            // input-independent frames) pins boss alignment at 0 <-> 0,
            // so input must land on exactly the same frame number.
            if (frame > 0) {
                const auto rin = replay.at(frame);
                in.left = rin.left;
                in.right = rin.right;
                in.jump = rin.up;
                in.fire = rin.attack;
            }
            if (frame > replay.last_frame() + 18) {
                res.quit = true;
                running = false;
            }
        } else {
            const Uint8* k = SDL_GetKeyboardState(nullptr);
            in.left = k[SDL_SCANCODE_LEFT] != 0 || gamepad::left();
            in.right = k[SDL_SCANCODE_RIGHT] != 0 || gamepad::right();
            in.jump = k[SDL_SCANCODE_UP] != 0 || gamepad::up();
            in.fire = k[SDL_SCANCODE_SPACE] != 0 || k[SDL_SCANCODE_LCTRL] != 0 ||
                      gamepad::attack_held();
        }
        // Record the resolved input at the frame the boss reader will resolve
        // it (at(frame)); frame 0 is input-free on replay, so skip it to match.
        if (input_rec.active() && frame > 0) {
            systems::FrameInputs fin;
            fin.left = in.left;
            fin.right = in.right;
            fin.up = in.jump;
            fin.attack = in.fire;
            input_rec.record(frame, fin);
        }

        // Previous-tick snapshot for the smooth-motion lerp (reference
        // boss runners: player x/y, L2 projectile x, L4 boss x/y).
        if (smooth) {
            player.prev_x = player.x;
            player.prev_y = player.y;
            for (auto& s : l2.slots) s.prev_x = s.x;
            l4.prev_x = l4.boss_x;
            l4.prev_y = l4.boss_y;
        }
        const int l4_health_before = l4.health;
        const int l6_health_before = l6.health;
        if (internal_level == 2) {
            update_l2_boss_frame(player, l2, in);
        } else if (internal_level == 4) {
            update_l4_boss_frame(player, l4, in);
        } else {
            update_l6_boss_frame(player, l6, in, smooth);
        }

        auto render_fight = [&]() {
            auto rt = make_target(fb);
            rt.use_float_pos = boss_use_float;
            rt.player_fx = boss_pfx;
            rt.player_fy = boss_pfy;
            if (internal_level == 2) {
                render_l2_frame(rt, assets, player, l2);
            } else if (internal_level == 4) {
                // During victory phases 1-3, use the victory renderer which
                // draws the correct player state (stand / rise / ride).
                if (l4.win_flag >= 1 && l4.win_flag < 100)
                    render_l4_victory_frame(rt, assets, player, l4);
                else
                    render_l4_frame(rt, assets, player, l4);
            } else {
                render_l6_frame(rt, assets, player, l6);
            }
        };

        // Render block.  Under smooth motion, L2/L4 draw 3 lerped
        // sub-frames per logic tick (54 Hz) — the reference boss
        // runners interpolate the player plus the L2 projectiles' x
        // and the L4 triceratops x/y, each behind the 16-px teleport
        // guard.  L6 interpolates the PLAYER too (the jump to the giant's
        // head) — matching Python run_l6_boss's sub-frame pass
        // (boss_l6.py apply_boss_player_interp); the giant is anchored so only
        // the player moves there.  Animation timers (jaw/arm, pip erasure)
        // tick once per logic frame, AFTER this block.
        if (smooth) {
            constexpr int kSnap = 16;   // reference _SNAP_THRESHOLD
            const int cur_px = player.x;
            const int cur_py = player.y;
            smooth_vsync_ran = smooth_fill_tick(pacer, [&](float alpha, int) {
                auto pair_lerp = [&](int& x, int& y, int prevx, int prevy,
                                     int curx, int cury) {
                    if (std::abs(curx - prevx) > kSnap ||
                        std::abs(cury - prevy) > kSnap) {
                        x = curx;
                        y = cury;
                        return;
                    }
                    x = prevx + static_cast<int>(
                                    std::lround((curx - prevx) * alpha));
                    y = prevy + static_cast<int>(
                                    std::lround((cury - prevy) * alpha));
                };
                pair_lerp(player.x, player.y, player.prev_x, player.prev_y,
                          cur_px, cur_py);
                // Part 1 follow-up: float player render pos (1-HD-px) consumed
                // by render_boss_player_fb via rt/wrt.player_fx (set in
                // render_fight + present_wide).  Same snap guard.
                if (std::abs(cur_px - player.prev_x) > kSnap ||
                    std::abs(cur_py - player.prev_y) > kSnap) {
                    boss_pfx = static_cast<float>(cur_px);
                    boss_pfy = static_cast<float>(cur_py);
                } else {
                    boss_pfx = player.prev_x + (cur_px - player.prev_x) * alpha;
                    boss_pfy = player.prev_y + (cur_py - player.prev_y) * alpha;
                }
                boss_use_float = true;
                std::array<int, 4> saved_sx{};
                int saved_bx = 0, saved_by = 0;
                if (internal_level == 2) {
                    for (std::size_t si = 0; si < l2.slots.size(); ++si) {
                        auto& s = l2.slots[si];
                        saved_sx[si] = s.x;
                        const int d = s.x - s.prev_x;
                        if (s.ptype != 0 && std::abs(d) <= kSnap) {
                            s.fx = s.prev_x + d * alpha;   // float shadow
                            s.x = s.prev_x +
                                  static_cast<int>(std::lround(d * alpha));
                        } else {
                            s.fx = static_cast<float>(s.x);
                        }
                    }
                } else if (internal_level == 4) {
                    saved_bx = l4.boss_x;
                    saved_by = l4.boss_y;
                    pair_lerp(l4.boss_x, l4.boss_y, l4.prev_x, l4.prev_y,
                              saved_bx, saved_by);
                    // float shadow (same 16px snap guard as pair_lerp)
                    if (std::abs(saved_bx - l4.prev_x) > kSnap ||
                        std::abs(saved_by - l4.prev_y) > kSnap) {
                        l4.fx = static_cast<float>(saved_bx);
                        l4.fy = static_cast<float>(saved_by);
                    } else {
                        l4.fx = l4.prev_x + (saved_bx - l4.prev_x) * alpha;
                        l4.fy = l4.prev_y + (saved_by - l4.prev_y) * alpha;
                    }
                }
                // L6: only the player is interpolated (giant is anchored).
                render_fight();
                present_any();
                player.x = cur_px;
                player.y = cur_py;
                if (internal_level == 2) {
                    for (std::size_t si = 0; si < l2.slots.size(); ++si) {
                        l2.slots[si].x = saved_sx[si];
                    }
                } else if (internal_level == 4) {
                    l4.boss_x = saved_bx;
                    l4.boss_y = saved_by;
                }
            });
            boss_use_float = false;   // non-smooth paths below draw integer
        } else {
            render_fight();
            present_any();
        }

        // F5 — write the bug report from the just-rendered native frame.
        if (bug_capture_pending) {
            bug_capture_pending = false;
            systems::SystemsState snap;   // boss arena: minimal state synth
            snap.player.x = player.x;
            snap.player.y = player.y;
            snap.player.lives = player.lives;
            snap.current_level = internal_level;   // boss display == internal
            snap.current_screen = 0;
            const bool want_presented = hd || ws_active;
            const std::string dir = write_bug_report(
                snap, fb, assets.spr, internal_level,
                internal_level, /*overlay_scale=*/1, BugAnnotations{},
                /*has_presented=*/want_presented);
            // screenshot_presented.png — the real on-screen boss frame (HD
            // upscale / widescreen letterbox), which the native `fb` shot
            // skips.  Re-render WITHOUT presenting so RenderReadPixels reads the
            // backbuffer (a post-present read is black on Metal), then read the
            // output-resolution pixels.  Mirrors the OLDUVAI_REAL_SHOT path.
            if (!dir.empty() && want_presented) {
                const bool l4v =
                    ws_active && internal_level == 4 && l4.win_flag >= 1;
                if (ws_active && !l4v) present_wide(true, /*do_present=*/false);
                else present_frame(true, /*do_present=*/false);
                int ow = 0, oh = 0;
                SDL_GetRendererOutputSize(ren, &ow, &oh);
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
                    0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
                if (s != nullptr &&
                    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                         s->pixels, s->pitch) == 0) {
                    save_surface_image(s, dir + "/screenshot_presented.png");
                }
                if (s != nullptr) SDL_FreeSurface(s);
            }
        }

        // Post-render ticks + HUD pip erasure — once per logic tick,
        // after the render block (the reference's draw-then-increment
        // order; jaw/arm timers must not fire 3× under sub-frames).
        bool won = false;
        if (internal_level == 2) {
            tick_l2_boss_post_render(player, l2);
            if (l2.health_column_pending) {
                // Classic only: enhanced uses vector bar (health tracked above)
                if (!(hd && hd_text.ok())) erase_pip_column(assets, l2.health);
                l2.health_column_pending = false;
            }
            won = l2.win_flag;
        } else if (internal_level == 4) {
            for (int h = l4.health; h < l4_health_before; ++h) {
                // Classic only: enhanced uses vector bar (health tracked above)
                if (!(hd && hd_text.ok())) erase_pip_column(assets, h + 1);
            }
            won = l4.win_flag >= 100;
        } else {
            tick_l6_boss_post_render(l6);
            for (int h = l6.health; h < l6_health_before; ++h) {
                // Classic only: enhanced uses vector bar (health tracked above)
                if (!(hd && hd_text.ok())) erase_pip_column(assets, h + 1);
            }
            won = l6.win_flag != 0;
        }
        if (audio != nullptr) {
            bool hit = false;
            if (internal_level == 2 && l2.sfx_hit_pending) {
                hit = true;
                l2.sfx_hit_pending = false;
            }
            if (internal_level == 4 && l4.sfx_hit_pending) {
                hit = true;
                l4.sfx_hit_pending = false;
            }
            if (internal_level == 6 && l6.sfx_hit_pending) {
                hit = true;
                l6.sfx_hit_pending = false;
            }
            if (hit) audio->play_sfx("SFX_HIT");
            if (player.jump_apex_sfx_pending) audio->play_sfx("SFX_JUMP_APEX");
        }
        player.jump_apex_sfx_pending = false;

        if (trace.active()) {
            const int bh = internal_level == 2   ? l2.health
                           : internal_level == 4 ? l4.health
                                                 : l6.health;
            trace.write_boss(frame, player, bh);
        }
        ++frame;
        if (!shot.empty() && frame == shot_frame) {
            if (std::getenv("OLDUVAI_REAL_SHOT") != nullptr) {
                // Debug: capture the ACTUAL window/fullscreen output (live
                // present → SDL logical-size scaling + letterbox bars), NOT the
                // offscreen ideal wide buffer — so we see the real on-screen
                // result (the squashing / bars the offscreen path hides).
                // Render WITHOUT presenting so RenderReadPixels sees the frame
                // before the buffer swap (post-present readback is black on Metal).
                const bool l4v =
                    ws_active && internal_level == 4 && l4.win_flag >= 1;
                if (ws_active && !l4v) present_wide(true, /*do_present=*/false);
                else present_frame(true, /*do_present=*/false);
                int ow = 0, oh = 0;
                SDL_GetRendererOutputSize(ren, &ow, &oh);
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
                    0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
                if (s != nullptr &&
                    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                         s->pixels, s->pitch) == 0) {
                    save_surface_image(s, shot);
                }
                if (s != nullptr) SDL_FreeSurface(s);
            } else if (ws_active) {
                // Widescreen HD: build the SAME wide upscaled scene buffer the
                // live present uses (compose + sprite overflow + whole-frame
                // upscale), bake the wide HUD into it at output resolution, and
                // save it straight to an offscreen SDL surface at the correct
                // 21:9 native dims (ws_w*hd_scale × 200*hd_scale).  RenderReadPixels
                // would capture the 16:10 WINDOW (wrong aspect) — the offscreen
                // surface preserves the true wide aspect (matches the spike).
                std::vector<std::uint8_t> up = build_wide_up();
                const int uw = ws_w * hd_scale, uh = 200 * hd_scale;
                if (hd_text.ok())
                    draw_boss_hud_into(up, uw, uh, /*draw_lives=*/true,
                                       /*cx_native=*/ws_M,
                                       /*total_native_w=*/ws_w);
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
                    0, uw, uh, 32, SDL_PIXELFORMAT_RGBA32);
                if (s != nullptr) {
                    for (int y = 0; y < uh; ++y)
                        std::memcpy(
                            static_cast<std::uint8_t*>(s->pixels) +
                                static_cast<std::size_t>(y) * s->pitch,
                            up.data() + static_cast<std::size_t>(y) * uw * 4,
                            static_cast<std::size_t>(uw) * 4);
                    save_surface_image(s, shot);
                    SDL_FreeSurface(s);
                }
            } else if (hd) {
                // HD: the vector HUD labels now live in the output-resolution
                // overlay (drawn after the scene RenderCopy), not in fb — so
                // capture the FINAL rendered output (scene + overlay).  Re-draw
                // scene + overlay WITHOUT presenting (RenderReadPixels reads the
                // backbuffer, which a prior present may have swapped away).
                SDL_UpdateTexture(tex, nullptr, fb.px.data(), fb.w * 4);
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                if (hd_text.ok()) draw_boss_hud_overlay(/*draw_lives=*/true);
                int ow = 0, oh = 0;
                SDL_GetRendererOutputSize(ren, &ow, &oh);
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
                    0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
                if (s != nullptr &&
                    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                                         s->pixels, s->pitch) == 0) {
                    save_surface_image(s, shot);
                }
                if (s != nullptr) SDL_FreeSurface(s);
            } else {
                SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
                    0, fb.w, fb.h, 32, SDL_PIXELFORMAT_RGBA32);
                std::memcpy(s->pixels, fb.px.data(), fb.px.size());
                save_surface_image(s, shot);
                SDL_FreeSurface(s);
            }
            running = false;
        }
        if (max_frames > 0 && frame >= max_frames) running = false;
        // Debug: OLDUVAI_FORCE_WIN=<frame> forces the win at that frame so the
        // victory sequence can be triggered + captured deterministically.
        if (const char* fw = std::getenv("OLDUVAI_FORCE_WIN")) {
            if (frame >= std::atoi(fw)) won = true;
        }
        if (won) {
            res.survived = true;
            running = false;
        }
        if (player.lives < 0) running = false;

        const Uint32 spent = SDL_GetTicks() - t0;
        // Drift-free DOS-rate pacing (see game_app: the relative SDL_Delay
        // oversleep made classic choppier than DOSBox).
        if (smooth_vsync_ran) {
            dos_ticker.arm();
        } else if (enhance.vga_scan && !hd && vga_scan_ok) {
            // --vga-scan hold-frame scanout + refused-vsync degradation
            // (see game_app tail).
            int fast_presents = 0;
            while (dos_ticker.pending()) {
                const Uint64 p0 = SDL_GetPerformanceCounter();
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                SDL_RenderPresent(ren);
                const double ms =
                    (SDL_GetPerformanceCounter() - p0) * 1000.0 /
                    static_cast<double>(SDL_GetPerformanceFrequency());
                if (ms < 1.5) {
                    if (++fast_presents >= 3) {
                        vga_scan_ok = false;
                        std::fprintf(stderr,
                                     "pacing: vsync appears refused — "
                                     "vga-scan off, timer pacing\n");
                        break;
                    }
                } else {
                    fast_presents = 0;
                }
            }
            if (dos_ticker.pending()) dos_ticker.wait_next();
            else dos_ticker.advance();
        } else {
            dos_ticker.wait_next();
        }
        (void)spent;
    }
    // Keep the boss music playing THROUGH the victory flash + ride-off:
    // The reference boss-L2 victory loop has no stop_music, so the
    // T-Rex track plays right up to the score tally, where BONUS.MDI takes
    // over (FUN_270a_01b4).  Only stop here when there is
    // no victory sequence to come (loss/quit) — otherwise the tally's
    // play_music(BONUS.MDI) replaces it.
    const bool victory_coming =
        res.survived && !res.quit && max_frames <= 0 && shot.empty();
    if (audio != nullptr && !victory_coming) audio->stop_music();
    res.lives = player.lives;
    res.score = player.score;

    // ── Victory sequences (F3) ───────────────────────────────────────────────
    // EXE: after the win condition each boss main calls Level_EndScreen(N,500):
    //   L2 T-Rex    23cf:0fc9 — Level_EndScreen(2, 500)
    //   L4 Tricera  24cc:0818 — Level_EndScreen(4, 500)
    //   L6 Giant    254f:0620 — Level_EndScreen(6, 500)
    if (res.survived && !res.quit && max_frames <= 0 &&
        (shot.empty() || std::getenv("OLDUVAI_REAL_SHOT") != nullptr)) {
        if (internal_level == 2) {
            // 18-frame flash; the EXE per-frame hold comes from the victory
            // lcall(15) routed through the delay shim at 1847:168f, which does
            // arg>>1 → 7 ticks (NOT 15).  7 ticks at 18 FPS ≈ 388 ms/frame
            // (18 × 388 ≈ 7 s total, not ~15 s).
            constexpr int kVictoryFrames = 18;
            constexpr Uint32 kFlashExtraMs = 388;   // 1000 * 7 / 18 ≈ 388
            for (int vf = 0; vf < kVictoryFrames && !res.quit; ++vf) {
                l2_last_flash = vf;   // remember for the post-victory fade parity
                // Widescreen: render the victory flash at NATIVE 320 and present
                // it wide (mirror + edge gradient) so it matches the fight; else
                // the unchanged HD pillarbox path.
                FrameBuffer vnat;   // native 320 for the wide victory path
                if (ws_active) {
                    RenderTarget rt{vnat.px.data(), 320, 200, 1, nullptr,
                                    nullptr};
                    render_l2_victory_frame(rt, assets, player, vf);
                } else {
                    auto rt = make_target(fb);
                    render_l2_victory_frame(rt, assets, player, vf);
                }
                // F4: HUD lives digit NOT drawn (EXE draw_lives=false), but
                // vector "LIVES:"/"ENERGY" labels ARE drawn.
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (handle_fullscreen_toggle(ev, win)) continue;
                    if (ev.type == SDL_QUIT ||
                        (ev.type == SDL_KEYDOWN &&
                         ev.key.keysym.sym == SDLK_ESCAPE)) {
                        res.quit = true;
                    }
                }
                if (!res.quit) {
                    if (std::getenv("OLDUVAI_REAL_SHOT") != nullptr && ws_active &&
                        vf == 8 && !shot.empty()) {
                        // Debug: capture a mid-flash victory frame (real output).
                        present_wide_native(vnat, /*draw_lives=*/false,
                                            /*do_present=*/false);
                        int ow = 0, oh = 0;
                        SDL_GetRendererOutputSize(ren, &ow, &oh);
                        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
                            0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
                        if (s != nullptr &&
                            SDL_RenderReadPixels(ren, nullptr,
                                                 SDL_PIXELFORMAT_RGBA32,
                                                 s->pixels, s->pitch) == 0)
                            save_surface_image(s, shot);
                        if (s != nullptr) SDL_FreeSurface(s);
                        res.quit = true;   // captured; stop the sequence
                    } else if (ws_active && wtex != nullptr) {
                        // Widescreen: build the wide buffer the SAME way the fight
                        // does — clean RING.PC1 arena composed wide (mirror + 0.10
                        // edge gradient) — then draw the victory SPRITES ONCE at
                        // origin_x = ws_M so the defeated T-Rex stays in the centre
                        // 320.  present_wide_native mirrored the baked 320 frame,
                        // reflecting the T-Rex tail at the screen edge into the
                        // margin (the "mirrored big T-Rex tail").  Same fix as the
                        // L4 ride-off victory.
                        last_wide_nat = vnat;   // keep the post-victory fade source
                        FrameBuffer cbg{320, 200};
                        std::copy(assets.bg.begin(), assets.bg.end(),
                                  cbg.px.begin());
                        std::vector<std::uint8_t> wide;
                        compose_widescreen(wide, ws_M, cbg, /*left=*/nullptr,
                                           /*right=*/nullptr, /*hud_rows=*/0,
                                           /*backdrop=*/nullptr,
                                           /*reflect_pure=*/true,
                                           /*margin_edge_brightness=*/0.10f);
                        RenderTarget wrt{wide.data(), ws_w, 200, 1, nullptr,
                                         nullptr};
                        wrt.origin_x = ws_M;
                        wrt.advance_state = false;
                        render_l2_victory_sprites(wrt, assets, player, vf);
                        std::vector<std::uint8_t> up = enhance::upscale_rgba(
                            wide, ws_w, 200, hd_scale, enhance.hd_profile);
                        SDL_UpdateTexture(wtex, nullptr, up.data(),
                                          ws_w * hd_scale * 4);
                        SDL_RenderClear(ren);
                        SDL_RenderCopy(ren, wtex, nullptr, nullptr);
                        if (hd_text.ok())
                            draw_boss_hud_overlay_wide(/*draw_lives=*/false);
                        SDL_RenderPresent(ren);
                        SDL_Delay(kFlashExtraMs);
                    } else if (ws_active) {
                        present_wide_native(vnat, /*draw_lives=*/false);  // resize fallback
                        SDL_Delay(kFlashExtraMs);
                    } else {
                        present_frame(/*draw_lives=*/false);
                        SDL_Delay(kFlashExtraMs);
                    }
                }
            }
        } else if (internal_level == 4) {
            // 3-phase ride-off (phase logic already in boss_l4.cpp update_victory;
            // the L4 win_flag was 100 when we exited the fight loop — reset to
            // phase 1 start because the fight loop exited when win_flag>=100 was
            // detected; all three phases already ran inside the fight loop
            // via update_l4_boss_frame → update_victory.  The fight loop exits
            // only when won=true (win_flag>=100), so by that point the ride-off
            // is complete.  No separate post-loop phase loop needed — the victory
            // rendered live inside the fight loop via render_l4_victory_frame.
            // Nothing to do here; fall through to fade.
            (void)l4;
        } else {
            // L6: player drops then 30-count victory sequence.
            // Build the static victory backdrop once (native 320×200 RGBA).
            FrameBuffer victory_bg_fb;   // always 320×200 for the blit helpers
            // Fill from current bg.
            std::copy(assets.bg.begin(), assets.bg.end(),
                      victory_bg_fb.px.begin());
            // H3.MAT sprite 0 (pose C, slam/resting) at (80,7) — the beaten pose.
            // boss_l6.py:98-100,164,591-595: H-MAT frame 2 = H3.MAT[0].
            // H1/H2/H3.MAT each contain exactly one sprite (one 240×190 pose),
            // so h1[2] is always OOB; correct source is assets.h3[0].
            {
                RenderTarget vt{victory_bg_fb.px.data(), 320, 200, 1,
                                nullptr, nullptr};
                blit_at(vt, assets.h3, 0, assets.palette, 80, 7);
                // L6SPR[49] at (213,7) — beaten face sprite.
                if (static_cast<int>(assets.spr.size()) > 49)
                    blit_sprite(vt, assets.spr[49], assets.palette, 213, 7);
            }

            // Widescreen L6 victory present: clean arena composed wide (mirror +
            // 0.10 edge gradient) with the victory SPRITES drawn ONCE at
            // origin_x = ws_M — so the beaten giant / landed player stay in the
            // centre 320 and are NOT reflected into the margin.  Replaces
            // present_wide_native (which mirrored the baked 320 frame → the "big
            // caveman reflection").  Same treatment as L2/L4.  Falls back to
            // present_wide_native if a resize drops the wide texture mid-sequence.
            auto present_l6_victory_wide = [&]() {
                rebuild_ws_if_resized();
                FrameBuffer vnat;
                {
                    RenderTarget rt{vnat.px.data(), 320, 200, 1, nullptr,
                                    nullptr};
                    render_l6_victory_frame(rt, victory_bg_fb.px, assets, player,
                                            l6);
                }
                if (!ws_active || wtex == nullptr) {
                    present_wide_native(vnat);   // resize fallback
                    return;
                }
                last_wide_nat = vnat;            // post-victory fade source
                FrameBuffer cbg{320, 200};
                std::copy(assets.bg.begin(), assets.bg.end(), cbg.px.begin());
                std::vector<std::uint8_t> wide;
                compose_widescreen(wide, ws_M, cbg, /*left=*/nullptr,
                                   /*right=*/nullptr, /*hud_rows=*/0,
                                   /*backdrop=*/nullptr, /*reflect_pure=*/true,
                                   /*margin_edge_brightness=*/0.10f);
                RenderTarget wrt{wide.data(), ws_w, 200, 1, nullptr, nullptr};
                wrt.origin_x = ws_M;
                wrt.advance_state = false;
                render_l6_victory_sprites(wrt, assets, player, l6);
                std::vector<std::uint8_t> up = enhance::upscale_rgba(
                    wide, ws_w, 200, hd_scale, enhance.hd_profile);
                SDL_UpdateTexture(wtex, nullptr, up.data(), ws_w * hd_scale * 4);
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, wtex, nullptr, nullptr);
                if (hd_text.ok()) draw_boss_hud_overlay_wide(/*draw_lives=*/true);
                SDL_RenderPresent(ren);
            };

            // Reset victory state in case fight loop already set win_flag=100.
            l6.win_flag = 1;
            l6.win_counter = 0;
            l6.cycle_idx = 0;
            l6.cycle_tick = 0;

            while (l6.win_flag != 100 && !res.quit) {
                const Uint32 vt0 = SDL_GetTicks();
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (handle_fullscreen_toggle(ev, win)) continue;
                    if (ev.type == SDL_QUIT ||
                        (ev.type == SDL_KEYDOWN &&
                         ev.key.keysym.sym == SDLK_ESCAPE)) {
                        res.quit = true;
                    }
                }

                // Snapshot player y before the logic tick (smooth-motion lerp).
                // cycle_idx / win_flag advance once per logic frame outside the
                // sub-frame loop — spec F3: only the drop is interpolated.
                const int prev_y = player.y;
                update_l6_victory_frame(player, l6);
                const int cur_y  = player.y;
                const bool is_dropping = (cur_y != prev_y);

                if (smooth && is_dropping) {
                    // 3 sub-frames at 54 Hz: lerp player y between prev and cur.
                    // Same pair_lerp guard as the fight loop (kSnap=16; drop is
                    // always +4 so it is always within the guard — kept for
                    // symmetry with the fight-loop pattern).
                    constexpr int kSnap = 16;
                    smooth_fill_tick(pacer, [&](float alpha, int) {
                        const int delta = cur_y - prev_y;
                        player.y = (std::abs(delta) > kSnap)
                                       ? cur_y
                                       : prev_y + static_cast<int>(
                                                      std::lround(delta * alpha));
                        if (ws_active) {
                            present_l6_victory_wide();   // clean bg + sprites@ws_M
                        } else {
                            auto rt = make_target(fb);
                            render_l6_victory_frame(rt, victory_bg_fb.px,
                                                    assets, player, l6);
                            present_frame();
                        }
                        player.y = cur_y;   // restore before next sub-frame
                    });
                } else {
                    if (ws_active) {
                        present_l6_victory_wide();   // clean bg + sprites@ws_M
                    } else {
                        auto rt = make_target(fb);
                        render_l6_victory_frame(rt, victory_bg_fb.px, assets,
                                                player, l6);
                        present_frame();
                    }
                    const Uint32 vspent = SDL_GetTicks() - vt0;
                    if (vspent < frame_ms) SDL_Delay(frame_ms - vspent);
                }
            }
        }
    }

    // ── Fade + score tally ───────────────────────────────────────────────────
    // Restore the pillarbox logical size before the fade + tally: these run on
    // the existing 320-wide lpresent path (out of scope for the wide work), so
    // returning the renderer to the aspect_logical fallback lets the 320 texture
    // pillarbox cleanly inside the wide window instead of stretching across it.
    if (ws_active) SDL_RenderSetLogicalSize(ren, _bld.w, _bld.h);
    if (res.survived && !res.quit) {
        if (ws_active) {
            // Widescreen: fade the LAST VICTORY frame (last_wide_nat) — NOT the
            // stale fight `fb` (which still holds player+club+enemies) — to
            // black, wrapped WIDE (mirror + 0.10 gradient, no HUD) so it matches
            // the victory.  Use the wide logical during the fade, then restore
            // the pillarbox logical for the tally (which appears on the now-black
            // screen, so its bezel bars are invisible).
            SDL_RenderSetLogicalSize(ren, ws_w * hd_scale, 200 * hd_scale);
            // L4 AND L2 fade through the OVERFLOW compose, not present_wide_native:
            // both have a victory sprite at the screen edge (L4's dino horn at
            // boss_x-55 ≈ 305; L2's defeated T-Rex tail), so mirroring the baked
            // last_wide_nat would DUPLICATE it into the margin (the "tail mirrored
            // at the edge" bug — it pops back in during the fade).  Build the wide
            // buffer once (clean bg mirror + the victory sprites drawn ONCE at
            // origin_x = ws_M, exactly like the victory present), then darken it
            // per frame — the edge sprite stays single.  L6 has the beaten giant
            // (240px pose, reaches the screen edge) + the landed player, so its
            // final frame ALSO mirrors into the margin (the "big caveman
            // reflection") — fade it through the same overflow compose.
            if (ws_active && wtex != nullptr &&
                (internal_level == 4 || internal_level == 2 ||
                 internal_level == 6)) {
                FrameBuffer cbg{320, 200};
                std::copy(assets.bg.begin(), assets.bg.end(), cbg.px.begin());
                std::vector<std::uint8_t> wbase;
                compose_widescreen(wbase, ws_M, cbg, /*left=*/nullptr,
                                   /*right=*/nullptr, /*hud_rows=*/0,
                                   /*backdrop=*/nullptr, /*reflect_pure=*/true,
                                   /*margin_edge_brightness=*/0.10f);
                RenderTarget wrt{wbase.data(), ws_w, 200, 1, nullptr, nullptr};
                wrt.origin_x = ws_M;
                wrt.advance_state = false;
                if (internal_level == 4)
                    render_l4_victory_sprites(wrt, assets, player, l4);
                else if (internal_level == 6)
                    render_l6_victory_sprites(wrt, assets, player, l6);
                else
                    render_l2_victory_sprites(wrt, assets, player, l2_last_flash);
                for (int f2 = 0; f2 <= kFadeFrames && !res.quit; ++f2) {
                    const double k =
                        1.0 - static_cast<double>(f2) / kFadeFrames;
                    std::vector<std::uint8_t> faded = wbase;
                    for (std::size_t i = 0; i < faded.size(); i += 4) {
                        faded[i]     = static_cast<std::uint8_t>(faded[i] * k);
                        faded[i + 1] = static_cast<std::uint8_t>(faded[i + 1] * k);
                        faded[i + 2] = static_cast<std::uint8_t>(faded[i + 2] * k);
                    }
                    std::vector<std::uint8_t> up = enhance::upscale_rgba(
                        faded, ws_w, 200, hd_scale, enhance.hd_profile);
                    SDL_UpdateTexture(wtex, nullptr, up.data(),
                                      ws_w * hd_scale * 4);
                    SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, wtex, nullptr, nullptr);
                    SDL_RenderPresent(ren);
                    SDL_Event ev;
                    while (SDL_PollEvent(&ev)) {
                        if (handle_fullscreen_toggle(ev, win)) continue;
                        if (ev.type == SDL_QUIT) res.quit = true;
                    }
                    SDL_Delay(frame_ms);
                }
            } else {
                for (int f2 = 0; f2 <= kFadeFrames && !res.quit; ++f2) {
                    FrameBuffer faded;
                    apply_fade(faded, last_wide_nat,
                               static_cast<double>(f2) / kFadeFrames);
                    present_wide_native(faded, /*draw_lives=*/false,
                                        /*do_present=*/true, /*draw_hud=*/false);
                    SDL_Event ev;
                    while (SDL_PollEvent(&ev)) {
                        if (handle_fullscreen_toggle(ev, win)) continue;
                        if (ev.type == SDL_QUIT) res.quit = true;
                    }
                    SDL_Delay(frame_ms);
                }
            }
            SDL_RenderSetLogicalSize(ren, _bld.w, _bld.h);
        } else {
            // Classic / non-widescreen: fade the arena frame to black.  The fade
            // applies to a native 320×200 FrameBuffer (lpresent upscales it).
            FrameBuffer fade_src;   // 320×200 for the lpresent path
            if (hd) {
                // Downscale the HD fb to 320×200 (nearest-neighbour).
                for (int y = 0; y < 200; ++y)
                    for (int x = 0; x < 320; ++x) {
                        const std::size_t so =
                            (static_cast<std::size_t>(y * hd_scale) * fb.w +
                             x * hd_scale) * 4;
                        const std::size_t do_ =
                            (static_cast<std::size_t>(y) * 320 + x) * 4;
                        fade_src.px[do_]     = fb.px[so];
                        fade_src.px[do_ + 1] = fb.px[so + 1];
                        fade_src.px[do_ + 2] = fb.px[so + 2];
                        fade_src.px[do_ + 3] = fb.px[so + 3];
                    }
            } else {
                fade_src = fb;   // classic: fb is already 320×200
            }
            FrameBuffer work;
            for (int f2 = 0; f2 <= kFadeFrames; ++f2) {
                apply_fade(work, fade_src,
                           static_cast<double>(f2) / kFadeFrames);
                if (!lpresent(work)) {
                    res.quit = true;
                    break;
                }
            }
        }
        if (!res.quit) {
            // Display level: internal boss levels 2/4/6 == display levels 2/4/6
            // (only L3↔L5 swap; boss slots are not affected).
            const int display_level = internal_level;
            // The score tally plays BONUS.MDI (FUN_270a_01b4,
            // play_music(MUSIC_BONUS)).  This replaces the boss track that was
            // still playing through the victory flash (FIX C).  BONUS.MDI lives
            // in FILESA.CUR (also FILESB.CUR); track id 1.  Buzzer variant
            // (BONUSBUZ.MDI when DS:0x8db5=='I') is a follow-up — the regular
            // BONUS.MDI is the default path.
            if (audio != nullptr && audio->music_available()) {
                // EXE Level_EndScreen fades the boss track out (MDI_FadeStop
                // 1f75:00e4) before starting BONUS.MDI (1f75:01bb) — match
                // that fade so the music is never abruptly cut.
                audio->fade_out_music();
                formats::CurArchive ba(slurp(game_dir / "FILESA.CUR"));
                formats::CurArchive bb(slurp(game_dir / "FILESB.CUR"));
                const std::vector<std::uint8_t>* md = nullptr;
                if (ba.contains("BONUS.MDI")) md = &ba.get("BONUS.MDI").data;
                else if (bb.contains("BONUS.MDI")) md = &bb.get("BONUS.MDI").data;
                if (md != nullptr) {
                    audio->play_music(*md, formats::mdi_track_id("bonus.mdi"));
                }
            }
            // skip is unused by show_score_tally (edge-triggered internally).
            const SkipFn no_skip = nullptr;
            // Enhanced tally: cartoon vector font at HD res.
            TallyHd tally_hd;
            if (hd && hd_text.ok()) {
                tally_hd.hd_text = &hd_text;
                tally_hd.scale = hd_scale;
                tally_hd.upscale =
                    [&](const std::vector<std::uint8_t>& px) {
                        return enhance::upscale_rgba(px, 320, 200, hd_scale,
                                                     enhance.hd_profile);
                    };
                tally_hd.present_hd =
                    [&](const std::vector<std::uint8_t>& hd_px, int w,
                        int h, const std::vector<HdTextRow>& rows) -> bool {
                        SDL_Event ev;
                        while (SDL_PollEvent(&ev)) {
                            if (handle_fullscreen_toggle(ev, win)) continue;
                            if (ev.type == SDL_QUIT ||
                                (ev.type == SDL_KEYDOWN &&
                                 ev.key.keysym.sym == SDLK_ESCAPE))
                                return false;
                        }
                        SDL_UpdateTexture(tex, nullptr, hd_px.data(), w * 4);
                        SDL_RenderClear(ren);
                        SDL_RenderCopy(ren, tex, nullptr, nullptr);
                        if (!rows.empty()) {
                            int ow = 0, oh = 0;
                            if (text_overlay.begin(ren, hd_text, ow, oh)) {
                                draw_tally_rows_overlay(
                                    text_overlay.buffer(), ow, oh, hd_text,
                                    rows);
                                text_overlay.flush(ren, logical_w, logical_h);
                            }
                        }
                        SDL_RenderPresent(ren);
                        SDL_Delay(1000 / 18);
                        (void)h;
                        return true;
                    };
            }
            if (!show_score_tally(res.lives, res.score, display_level, 500,
                                  assets.charset, assets.palette,
                                  lpresent, no_skip, tally_hd,
                                  TallyAudio{audio, enhance.flags.cinematic_cue})) {
                res.quit = true;
            }
        }
    }
    // ── End victory block ────────────────────────────────────────────────────

    hd_cache.clear();
    if (pause_tex != nullptr) SDL_DestroyTexture(pause_tex);
    SDL_DestroyTexture(tex);
    if (wtex != nullptr) SDL_DestroyTexture(wtex);
    return res;
}

}  // namespace olduvai::presentation
