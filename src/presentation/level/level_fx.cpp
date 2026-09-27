// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/level/level_fx.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "presentation/level/level_setup.hpp"   // refresh_secret_tiles
#include "systems/fluid_bubbles.hpp"            // kSnapThreshold

namespace olduvai::presentation {

namespace {

// Enhanced L3 dust tail: faint puffs for ~2 s on screen 18 after the trunk
// lands (the EXE cuts the smoke at landing).  Hash jitter, never the game LCG.

void draw_l3_smoke_tail(RenderTarget& t, int ticks,
                        const LevelRenderAssets& r) {
    if (ticks <= 0) return;
    const auto& spr = r.entity_sprites;
    const auto hash_jit = [](int i, int k) -> int {
        std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u ^
                          (static_cast<std::uint32_t>(k) * 0x9E3779B9u);
        return static_cast<int>((h >> 16) & 7u);
    };
    // Dissipate by count (3, 2, 1 puffs), not alpha: indexed-palette sprites.
    const int n = 1 + (ticks * 3) / (kL3SmokeTailTicks + 1);
    static constexpr int kTailX[3] = {65, 49, 95};
    for (int k = 0; k < n; ++k) {
        const int idx = 85 + (((ticks >> 1) + k) & 1);   // 85/86
        if (idx < static_cast<int>(spr.size()))
            blit_sprite(t, spr[static_cast<std::size_t>(idx)], r.palette,
                        kTailX[k], 173 + hash_jit(ticks, k));
    }
}

// Enhanced teleport clouds (cave signs), armed in collision_dispatch; each
// stage holds 3 ticks.  Departure: 9 ticks, clouds 87/86/85 shrinking.
// Arrival: 12 ticks, a 3-tick empty beat then 85/86/87 growing.  Depart
// ticks 12-10 and arrive ticks 3-1 show PLAYER_TURN (134), no cloud.
// Feet-anchored on the 32x30 player box.  Tables match the reference.
void draw_teleport_fx(RenderTarget& t, const systems::SystemsState& st,
                      const LevelRenderAssets& r) {
    int idx = -1;
    if (st.teleport_out_ticks > 0) {
        if (st.teleport_out_ticks <= 9)
            idx = 85 + (st.teleport_out_ticks - 1) / 3;
    } else if (st.teleport_in_ticks > 0) {
        const int tk = st.teleport_in_ticks;
        if (tk >= 4 && tk <= 12) idx = 85 + (12 - tk) / 3;
    }
    if (idx < 0 || idx >= static_cast<int>(r.entity_sprites.size())) return;
    const auto& spr = r.entity_sprites[static_cast<std::size_t>(idx)];
    blit_sprite(t, spr, r.palette, st.teleport_fx_x + (32 - spr.width) / 2,
                st.teleport_fx_y + (30 - spr.height));
}

}  // namespace

void LevelFx::step(const Loaded& g) {
    const auto& st = g.state;
    balloons.step(st.enhanced_active,
                  st.current_level == 1 && st.glider_active,
                  st.player.death_counter == 0, st.player.x,
                  st.player.y - 30, st.current_screen);
}

void LevelFx::draw(RenderTarget& t, const Loaded& g) const {
    draw_l3_smoke_tail(t, l3_smoke_tail, g.render);
    draw_teleport_fx(t, g.state, g.render);
    balloons.draw(t, g.render.entity_sprites, g.render.palette, alpha);
}

// 8c. Secret-room bubble scatter: exactly one 627-draw LCG pass per frame,
// here in the render gate (the reference runs it as logic step 8c).  The
// enhanced fluid bubbles tick after it: their PRNG is separate.
void secret_room_pass(Loaded& g, bool fluid) {
    if (!g.state.secret_flag) return;
    refresh_secret_tiles(g, /*draw_scatter=*/!fluid);
    if (fluid) g.fluid_bubbles.tick();
}

// Enhanced secret room: the 60 rising bubbles (ELEML1.MAT sprites 17/18),
// drawn before the tiles so the floor (y=168) covers them as they emerge.
// Widescreen mirrors each above-floor bubble into both margins, matching
// compose_widescreen's self-tile mapping (left -1-x, right 639-x in centre
// coordinates); below the floor the margin is not redrawn over them.
std::function<void(RenderTarget&)> make_bubble_hook(const Loaded& g,
                                                    bool ws_mirror) {
    return [&g, ws_mirror](RenderTarget& frame) {
        const auto& sprites = g.render.tile_sprites;
        for (const auto& b : g.fluid_bubbles.bubbles()) {
            if (b.sprite_idx < 0 ||
                b.sprite_idx >= static_cast<int>(sprites.size()))
                continue;
            const formats::Sprite& spr =
                sprites[static_cast<std::size_t>(b.sprite_idx)];
            // Float position, rounded at HD: slow bubbles still move every
            // sub-frame.
            blit_sprite_keyed(frame, spr, g.render.palette, b.x, b.y);
            if (ws_mirror && b.y < 168.0f) {
                blit_sprite_keyed(frame, spr, g.render.palette, -1.0f - b.x,
                                  b.y);
                blit_sprite_keyed(frame, spr, g.render.palette, 639.0f - b.x,
                                  b.y);
            }
        }
    };
}

// The fluid bubbles at a smooth sub-frame, behind the same 16 px snap guard
// as everything else (a respawn has prev == cur).  Returns the logic
// positions for restore_fluid_bubbles.
std::vector<BubblePos> lerp_fluid_bubbles(Loaded& g, float alpha) {
    auto& bubbles = g.fluid_bubbles.bubbles_mutable();
    std::vector<BubblePos> saved;
    saved.reserve(bubbles.size());
    for (auto& b : bubbles) {
        saved.push_back({b.x, b.y});
        const float dx = b.x - b.prev_x;
        const float dy = b.y - b.prev_y;
        if (std::abs(dx) <= systems::kSnapThreshold &&
            std::abs(dy) <= systems::kSnapThreshold) {
            b.x = b.prev_x + dx * alpha;
            b.y = b.prev_y + dy * alpha;
        }
    }
    return saved;
}

void restore_fluid_bubbles(Loaded& g, const std::vector<BubblePos>& saved) {
    auto& bubbles = g.fluid_bubbles.bubbles_mutable();
    for (std::size_t i = 0; i < bubbles.size() && i < saved.size(); ++i) {
        bubbles[i].x = saved[i].x;
        bubbles[i].y = saved[i].y;
    }
}

// OLDUVAI_BUBBLE_TRACE=1: the slowest moving bubble (vy ~1, ~0.33 px per
// sub-frame) at this sub-frame, with native and HD rounding: native collapses
// its three sub-frames to one pixel.
void trace_slow_bubble(const Loaded& g, int frame, int sub, int scale) {
    for (const auto& b : g.fluid_bubbles.bubbles()) {
        const float dy = std::abs(b.y - b.prev_y);
        if (b.vy <= 1.2f && dy > 0.01f && dy <= systems::kSnapThreshold) {
            std::fprintf(stderr,
                         "[BUBTRACE] vy=%.2f f=%d sub=%d y=%.3f "
                         "native_int=%d hd_int=%ld\n",
                         b.vy, frame, sub, b.y, static_cast<int>(b.y),
                         std::lround(b.y * scale));
            return;
        }
    }
}

}  // namespace olduvai::presentation
