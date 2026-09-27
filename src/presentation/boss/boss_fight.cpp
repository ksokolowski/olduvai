// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/boss/boss_fight.hpp"

#include "presentation/game_app.hpp"   // GameOptions

#include <array>
#include <cstddef>

#include <SDL.h>

#include "formats/pc1.hpp"
#include "prepare/game_archives.hpp"
#include "presentation/input/gamepad.hpp"
#include "presentation/render/lerp_snapshot.hpp"   // snap_lerp_*

namespace olduvai::presentation {

using namespace olduvai::systems;

namespace {

// Consume a boss's one-shot hit-sound flag.
bool take_flag(bool& pending) {
    const bool was = pending;
    pending = false;
    return was;
}

}  // namespace

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

void drain_pip_column(BossFight& f, BossAssets& a, int column, bool paint) {
    f.drained_columns.push_back(column);
    if (paint) erase_pip_column(a, column);
}

void rebuild_arena_bg(BossAssets& a, const BossFight& f, bool vector_hud) {
    a.bg = a.bg_source;
    if (vector_hud) return;
    for (const int column : f.drained_columns) erase_pip_column(a, column);
}

bool load_boss_assets(const std::filesystem::path& dir, int level,
                      BossAssets& a) {
    const prepare::GameArchives archives(dir);
    auto entry = [&](const std::string& n) -> const std::vector<std::uint8_t>* {
        return archives.entry(n);
    };
    const auto* ring = entry("RING.PC1");
    const auto* font = entry("CHARSET1.MAT");
    if (ring == nullptr || font == nullptr) return false;
    a.charset = formats::load_mat_sprites(font, "CHARSET1.MAT");
    a.bone_atlas = formats::load_mat_sprites(entry("L1SPR.MAT"), "L1SPR.MAT");
    if (const auto* f1 = entry("FOND1.PC1"))
        a.bone_palette = formats::parse_pc1(*f1).palette;
    const auto img = formats::parse_pc1(*ring);
    a.palette = img.palette;
    if ((level == 2 || level == 4) && a.palette.size() > 5) {
        // DAC index 5 -> dark red (triceratops tail-on-hit + entry balloons).
        // L2: FUN_23cf 0x0a4f; L4: FUN_24cc_02f2 0x031f-0x0329 writes
        // DS:0x91b9=0x20, DS:0x8a6f=0, DS:0x8e43=8 -> RGB 130,0,32.  RING.PC1
        // ships it brown; L6 keeps brown.
        a.palette[5] = {130, 0, 32};
    }
    a.bg_source.assign(320 * 200 * 4, 0);
    indexed_to_rgba(img.pixels, a.palette, a.bg_source.data(), 320 * 200);
    a.bg = a.bg_source;
    const char* spr_name = level == 2 ? "L2SPR.MAT"
                          : level == 4 ? "L4SPR.MAT" : "L6SPR.MAT";
    a.spr = formats::load_mat_sprites(entry(spr_name), spr_name);
    if (level == 2) {
        a.elem = formats::load_mat_sprites(entry("ELEML2.MAT"), "ELEML2.MAT");
    }
    if (level == 6) {
        // Body poses A/B/C are single-sprite MATs H1/H2/H3, indexed directly by
        // the attack-frame table, so all must load; H4 is the head sheet (3
        // heads + shocked).
        a.h1 = formats::load_mat_sprites(entry("H1.MAT"), "H1.MAT");
        a.h2 = formats::load_mat_sprites(entry("H2.MAT"), "H2.MAT");
        a.h3 = formats::load_mat_sprites(entry("H3.MAT"), "H3.MAT");
        a.h4 = formats::load_mat_sprites(entry("H4.MAT"), "H4.MAT");
    }
    return !a.spr.empty();
}

void save_prev_positions(BossFight& f) {
    f.player.prev_x = f.player.x;
    f.player.prev_y = f.player.y;
    for (auto& slot : f.l2.slots) slot.prev_x = slot.x;
    f.l4.prev_x = f.l4.boss_x;
    f.l4.prev_y = f.l4.boss_y;
}

FightPositions interpolate_fight(BossFight& f, float alpha, SmoothPos& sp) {
    FightPositions s;
    s.player_x = f.player.x;
    s.player_y = f.player.y;
    // Integer shadow and float render position from one guarded decision.  No
    // screen changes in an arena, so no force signal.
    const auto pp = snap_lerp_pair(f.player.prev_x, f.player.prev_y,
                                   s.player_x, s.player_y, alpha);
    f.player.x = pp.x;
    f.player.y = pp.y;
    sp = {true, pp.fx, pp.fy};
    if (f.level == 2) {
        for (std::size_t si = 0; si < f.l2.slots.size(); ++si) {
            auto& slot = f.l2.slots[si];
            s.slot_x[si] = slot.x;
            // A projectile has only x.  Inactive slots (ptype 0) are not
            // interpolated.
            if (slot.ptype != 0) {
                slot.fx = snap_lerp_f(slot.prev_x, s.slot_x[si], alpha);
                slot.x = snap_lerp_i(slot.prev_x, s.slot_x[si], alpha);
            } else {
                slot.fx = static_cast<float>(slot.x);
            }
        }
    } else if (f.level == 4) {
        s.boss_x = f.l4.boss_x;
        s.boss_y = f.l4.boss_y;
        // The dino moves diagonally: a teleport on either axis must snap both.
        const auto bp = snap_lerp_pair(f.l4.prev_x, f.l4.prev_y, s.boss_x,
                                       s.boss_y, alpha);
        f.l4.boss_x = bp.x;
        f.l4.boss_y = bp.y;
        f.l4.fx = bp.fx;
        f.l4.fy = bp.fy;
    }
    return s;
}

void restore_fight(BossFight& f, const FightPositions& s) {
    f.player.x = s.player_x;
    f.player.y = s.player_y;
    if (f.level == 2) {
        for (std::size_t si = 0; si < f.l2.slots.size(); ++si)
            f.l2.slots[si].x = s.slot_x[si];
    } else if (f.level == 4) {
        f.l4.boss_x = s.boss_x;
        f.l4.boss_y = s.boss_y;
    }
}

BossOps make_boss_ops(BossFight& f, const BossAssets& a,
                      const int& l2_last_flash, bool smooth) {
    BossOps o;
    if (f.level == 2) {
        o.render_frame = [&f, &a](RenderTarget& t) {
            render_l2_frame(t, a, f.player, f.l2);
        };
        o.render_sprites = [&f, &a](RenderTarget& t) {
            render_l2_sprites(t, a, f.player, f.l2);
        };
        o.render_fight_frame = o.render_frame;
        o.update_frame = [&f](const BossInputs& i) {
            update_l2_boss_frame(f.player, f.l2, i);
        };
        o.take_sfx_hit = [&f] { return take_flag(f.l2.sfx_hit_pending); };
        o.render_victory_sprites = [&f, &a, &l2_last_flash](RenderTarget& t) {
            render_l2_victory_sprites(t, a, f.player, l2_last_flash);
        };
        o.phase = [&f] {
            return "jaw " + std::to_string(f.l2.jaw_state) + ", arm " +
                   std::to_string(f.l2.arm_frame) + ", stomp timer " +
                   std::to_string(f.l2.stomp_timer) +
                   (f.l2.win_flag ? ", won" : "");
        };
    } else if (f.level == 4) {
        o.render_frame = [&f, &a](RenderTarget& t) {
            render_l4_frame(t, a, f.player, f.l4);
        };
        o.render_sprites = [&f, &a](RenderTarget& t) {
            render_l4_sprites(t, a, f.player, f.l4);
        };
        // Victory phases 1-3 draw the stand/rise/ride pose.
        o.render_fight_frame = [&f, &a](RenderTarget& t) {
            if (f.l4.win_flag >= 1 && f.l4.win_flag < 100)
                render_l4_victory_frame(t, a, f.player, f.l4);
            else
                render_l4_frame(t, a, f.player, f.l4);
        };
        o.update_frame = [&f](const BossInputs& i) {
            update_l4_boss_frame(f.player, f.l4, i);
        };
        o.take_sfx_hit = [&f] { return take_flag(f.l4.sfx_hit_pending); };
        o.render_victory_sprites = [&f, &a](RenderTarget& t) {
            render_l4_victory_sprites(t, a, f.player, f.l4);
        };
        o.phase = [&f] {
            return "at (" + std::to_string(f.l4.boss_x) + ", " +
                   std::to_string(f.l4.boss_y) + "), stun " +
                   std::to_string(f.l4.stun_counter) + ", spring " +
                   std::to_string(f.l4.spring_state) + ", victory " +
                   std::to_string(f.l4.win_flag);
        };
    } else {
        o.render_frame = [&f, &a](RenderTarget& t) {
            render_l6_frame(t, a, f.player, f.l6);
        };
        o.render_sprites = [&f, &a](RenderTarget& t) {
            render_l6_sprites(t, a, f.player, f.l6);
        };
        o.render_fight_frame = o.render_frame;
        o.update_frame = [&f, smooth](const BossInputs& i) {
            update_l6_boss_frame(f.player, f.l6, i, smooth);
        };
        o.take_sfx_hit = [&f] { return take_flag(f.l6.sfx_hit_pending); };
        o.render_victory_sprites = [&f, &a](RenderTarget& t) {
            render_l6_victory_sprites(t, a, f.player, f.l6);
        };
        o.phase = [&f] {
            return "frame " + std::to_string(f.l6.frame_counter) +
                   ", punch " + std::to_string(f.l6.ground_punch_state) +
                   ", hit reaction " +
                   std::to_string(f.l6.hit_reaction_counter) + ", victory " +
                   std::to_string(f.l6.win_flag);
        };
    }
    o.health = [&f] { return f.health(); };
    return o;
}

BossInputs read_boss_inputs(const InputReplay& replay, int frame) {
    BossInputs in;
    if (replay.active()) {
        if (frame > 0) {
            const auto rin = replay.at(frame);
            in.left = rin.left;
            in.right = rin.right;
            in.jump = rin.up;
            in.fire = rin.attack;
        }
        return in;
    }
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    in.left = k[SDL_SCANCODE_LEFT] != 0 || gamepad::left();
    in.right = k[SDL_SCANCODE_RIGHT] != 0 || gamepad::right();
    in.jump = k[SDL_SCANCODE_UP] != 0 || gamepad::up();
    in.fire = k[SDL_SCANCODE_SPACE] != 0 || k[SDL_SCANCODE_LCTRL] != 0 ||
              gamepad::attack_held();
    return in;
}

BossOps draw_after(BossOps ops,
                   const std::function<void(RenderTarget&)>& effect) {
    for (auto* fn : {&ops.render_frame, &ops.render_sprites,
                     &ops.render_fight_frame}) {
        *fn = [inner = std::move(*fn), effect](RenderTarget& t) {
            inner(t);
            effect(t);
        };
    }
    return ops;
}

bool boss_smooth_motion(const GameOptions& opts, bool force) {
    return opts.enhance.smooth_motion &&
           (force || (opts.frames <= 0 && opts.screenshot.empty()));
}

void record_boss_inputs(InputRecorder& rec, int frame, const BossInputs& in) {
    if (!rec.active() || frame <= 0) return;
    systems::FrameInputs fin;
    fin.left = in.left;
    fin.right = in.right;
    fin.up = in.jump;
    fin.attack = in.fire;
    rec.record(frame, fin);
}

bool boss_post_render(BossFight& f, BossAssets& a, int l4_health_before,
                      int l6_health_before, bool erase_pips) {
    if (f.level == 2) {
        tick_l2_boss_post_render(f.player, f.l2);
        if (f.l2.health_column_pending) {
            drain_pip_column(f, a, f.l2.health, erase_pips);
            f.l2.health_column_pending = false;
        }
        return f.l2.win_flag;
    }
    if (f.level == 4) {
        for (int h = f.l4.health; h < l4_health_before; ++h)
            drain_pip_column(f, a, h + 1, erase_pips);
        return f.l4.win_flag >= 100;
    }
    tick_l6_boss_post_render(f.l6);
    for (int h = f.l6.health; h < l6_health_before; ++h)
        drain_pip_column(f, a, h + 1, erase_pips);
    return f.l6.win_flag != 0;
}

}  // namespace olduvai::presentation
