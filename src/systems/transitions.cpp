// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/transitions.hpp"

#include "core/constants.hpp"
#include "core/rng.hpp"

namespace olduvai::systems {

void check_l1_transition(SystemsState& state) {
    PlayerState& p = state.player;
    const int scr = state.current_screen;

    // Left edge.
    if (p.x < 6 && !state.glider_active && scr > 0) {
        state.current_screen -= 1;
        p.x = 285;
        state.screen_change = true;
        return;
    }
    // Screen-9 balloon-ride entry.
    if (scr == 9 && p.x > 260 && !state.glider_active) {
        p.x += 2;
        state.glider_active = true;
        return;
    }
    // Screen-9 ride exit.
    if (scr == 9 && p.x > 299 && state.glider_active) {
        state.current_screen += 1;
        p.x = 6;
        state.screen_change = true;
        return;
    }
    // Normal right edge (screens 9 and 18 excluded).
    if (p.x > 295 && scr != 9 && scr != core::kLastScreen) {
        state.current_screen += 1;
        p.x = 6;
        state.screen_change = true;
        return;
    }
    // Food gate on the last screen.
    if (scr == core::kLastScreen && p.x > 260 &&
        state.food_count >= kFoodGate) {
        state.current_screen += 1;
        p.x = 6;
        state.level_complete = true;
    }
}

void check_l3_transition(SystemsState& state) {
    PlayerState& p = state.player;
    const int scr = state.current_screen;
    if (scr == 12 && !state.screen_change && p.x < 10) p.x = 10;
    if (scr == 18 && p.x < 0x14) p.x = 0x14;
    if (scr == 11) {
        if (p.x < 100 && p.y > 0x22) p.x = 100;
        else if (p.x > 0xCC) p.x = 0xCC;
    }
    if (scr == 10) {
        if (p.x < 100) p.x = 100;
        if (p.x > 0xCC) p.x = 0xCC;
    }
    if (scr == 9 && !state.cave_flag && p.x > 0x10E) p.x = 0x10E;
    // The screen-9 tree-trunk warp trigger (DOWN in the doorway).
    if (scr == 9 && !state.screen_change && p.cave_warp_freeze == 0 &&
        p.x > 0x70 && p.x < 0x7C && p.y == 0x4C && state.input.down) {
        p.x = 0x77;
        p.cave_warp_freeze = 0xFA1;
    }
    if (scr == 18 && !state.screen_change && p.x > 299) {
        state.current_screen += 1;
        state.screen_change = true;
        state.level_complete = true;
        return;
    }
    if (scr == 11 && !state.screen_change) {
        if (p.y > 0xA4) {            // fall back down 11 -> 10
            state.current_screen -= 1;
            p.y = -1;
            state.screen_change = true;
            return;
        }
        if (p.y < 0x87 && p.x < 0x5A) {   // climb out 11 -> 12
            state.current_screen += 1;
            p.x = 0x96;
            p.y = 0x32;
            state.screen_change = true;
            // Trunk-cave EXIT to surface — EXE FUN_2276_06f2 0x0c82-0x0cbf
            // clears bp-6 = 0 at 0x0cbf, routing the next frame through
            // Sprite_DrawDispatch mode=2 (the fade-out → blit → fade-in pair).
            state.player.cave_warp_pending = true;
            return;
        }
    }
    if (scr == 10 && !state.screen_change) {
        if (p.y < -1 && state.input.jump) {   // jump up 10 -> 11
            state.current_screen += 1;
            state.screen_change = true;
            p.y = 0xA4;
            return;
        }
    }
    if (p.x < 6 && !state.glider_active && scr != 0 && !state.screen_change) {
        state.current_screen -= 1;
        p.x = 285;
        state.screen_change = true;
        return;
    }
    if (p.x > 295 && scr != 18 && !state.screen_change) {
        state.current_screen += 1;
        p.x = 6;
        state.screen_change = true;
        return;
    }
    // Screen-17 food gate: food > 44 at the gate spot with the screen clear.
    if (scr == 17 && state.food_count > 0x2C && !state.screen_change &&
        p.y == 0x44 && state.screen_clear_of_monsters) {
        state.current_screen += 1;
        state.screen_change = true;
    }
}

void check_l5_transition(SystemsState& state) {
    PlayerState& p = state.player;
    const int scr = state.current_screen;
    if ((scr == 9 || scr == 18) && state.glider_active) {
        p.x += 3;
        p.y += 1;
    }
    if (scr == 18 && state.glider_active && p.x > 300) {
        state.current_screen = 19;
        state.screen_change = true;
        state.level_complete = true;
        return;
    }
    if (p.x < 6 && !state.glider_active && scr > 0 && !state.screen_change) {
        state.current_screen -= 1;
        p.x = 285;
        state.screen_change = true;
        return;
    }
    if (p.x > 295 && scr != core::kLastScreen && !state.screen_change) {
        state.current_screen += 1;
        if (state.current_screen == 12 && state.glider_active) p.y = 70;
        p.x = 6;
        state.screen_change = true;
    }
}

// Warp behaviour here is the AUTHORITY (capstone-cited); the seam
// topology it implies is mirrored in systems/screen_topology.hpp
// (one table for peek suppression + transition-kind classification).
// test_screen_topology cross-checks the two.
void check_l7_transition(SystemsState& state) {
    PlayerState& p = state.player;
    int scr = state.current_screen;
    if (scr == 18 && p.x > 0x132) p.x = 0x132;
    // Screen-18 lava-spring exit trigger.
    if (scr == 18 && !state.screen_change &&
        state.screen_clear_of_monsters && p.cave_warp_freeze == 0 &&
        state.food_count > 0x2C && state.input.down &&
        p.x > 0xEB && p.x < 0xF5 && p.y == 0x7F) {
        p.cave_warp_freeze = 0xFA1;
        p.x = 0xF0;
    }
    if (scr == 0xD && p.x < 0xF) p.x = 0xF;
    if (scr == 5 && p.x > 0x109) p.x = 0x109;
    if (scr == 5 && p.y > 0xA0) {       // fall through 5 -> 6
        state.current_screen = 6;
        p.y = -10;
        state.screen_change = true;
        return;
    }
    if (state.current_screen == 6) {
        if (p.x < 0x1E) p.x = 0x1E;
        if (p.x > 0x109) p.x = 0x109;
        if (p.y > 0xA0) {               // fall through 6 -> 7
            state.current_screen = 7;
            p.y = 0;
            state.screen_change = true;
            return;
        }
    }
    if (state.current_screen == 7 && p.x < 0xF) p.x = 0xF;
    if (state.current_screen == 9 && p.x > 0x109) p.x = 0x109;
    if (state.current_screen == 10 && p.x < 7) p.x = 7;
    if (p.x < 6 && state.current_screen != 0 && !state.screen_change) {
        state.current_screen -= 1;
        p.x = 0x11D;
        state.screen_change = true;
        return;
    }
    if (p.x > 0x127 && state.current_screen != 0x12 &&
        !state.screen_change) {
        state.current_screen += 1;
        if (state.current_screen == 9) p.y -= 0x10;
        p.x = 6;
        if (state.current_screen == 0xD) {   // teleport entry, no scroll
            p.x = 0x30;
            p.y = 130;
        }
        state.screen_change = true;
    }
}

void check_screen_transition(SystemsState& state) {
    switch (state.current_level) {
        case 1: check_l1_transition(state); break;
        case 3: check_l3_transition(state); break;
        case 5: check_l5_transition(state); break;
        case 7: check_l7_transition(state); break;
        default: break;
    }
}

void check_cave_warp_animation(SystemsState& state) {
    if (state.current_level != 3 && state.current_level != 7) return;
    PlayerState& p = state.player;
    if ((p.cave_warp_freeze & 3) == 0) return;
    p.cave_warp_freeze >>= 2;
    if (p.cave_warp_freeze != 0x3E8) return;   // animate until 1000
    if (state.current_level == 3) {
        state.current_screen = 0xA;
        p.x = 0x64;
        p.y = 0x9F;
        state.screen_change = true;
    } else {
        if (state.current_screen == 9) {
            state.current_screen = 10;
            p.x = 0xA;
            p.y = 0x83;
            state.screen_change = true;
        } else if (state.current_screen == 18) {
            state.current_screen = 19;
            state.level_complete = true;
            state.screen_change = true;
        }
    }
}

void check_l5_glider_entry(SystemsState& state) {
    if (state.current_level != 5 || state.glider_active) return;
    if (state.screen_change) return;
    PlayerState& p = state.player;
    if (p.death_counter > 0) return;
    const int scr = state.current_screen;
    if (scr == 9 && p.x > 215 && p.y < 20) {
        state.glider_active = true;
        p.sprite = 15;            // climb pose at the grab
        p.hit_counter = 0x0F;     // freeze frames
    }
    if (scr == 18 && p.x > 140 && p.y < 3 &&
        state.food_count >= kFoodGate) {
        state.glider_active = true;
        p.sprite = 39;            // the screen-18 grab pose
        p.hit_counter = 0x27;
    }
}

void handle_l5_screen12_glider(SystemsState& state) {
    if (state.current_level != 5 || state.current_screen != 12) return;
    if (state.glider_active) {
        state.glider_x = state.player.x;
        state.glider_y = state.player.y;
        state.glider_active = false;
        state.player.platform_flag = 0;
        return;
    }
    if (state.glider_y > -30) {   // fly away up-right
        state.glider_x += 5;
        state.glider_y -= 4;
    }
}

void roll_l3_descent_smoke_jitter(SystemsState& state) {
    // FUN_2276_03d9:0x0554 + 0x0586 — smoke A then B per outer iter, iters
    // 0..19 only (iter 20 jumps to exit at 0x0516-0x051c).  Two Rand_LCG16()
    // % 10 calls per iter = 40 global LCG draws per descent.  The renderer
    // reads the cached pairs; the LCG state is thus advanced identically
    // whether we are in interactive, replay, or headless mode.
    // Capstone-verified 2026-06-12: same modulus 0x0a, same call site order.
    auto& rng = core::global_rng();
    for (auto& p : state.l3_descent_smoke_jitter) {
        p.first  = static_cast<int>(rng.next() % 10);  // jitter_a (smoke A Y)
        p.second = static_cast<int>(rng.next() % 10);  // jitter_b (smoke B Y)
    }
}

void clear_per_screen_state(SystemsState& state) {
    state.fireball_flag = 0;     // DS:0x97e4
    state.bonus_trigger = 0;     // DS:0x9894
    state.player.platform_flag = 0;
    state.player.cave_warp_freeze = 0;   // DS:0x987e
    state.player.cave_warp_pending = false;   // presentation transient
    // Floating score popups are zeroed on EVERY screen change: each surface
    // level main re-runs a clear loop over the 10-slot popup array (DS:0x9806,
    // stride 8) at its per-screen setup top, zeroing every counter field
    // (L1-main capstone 0x289 zero-writes each slot counter at DS:0x9806+bx;
    // L5 0x263, L3 0x093a, L7 0x03e9; cave init 0x07b5).  The in-screen frame
    // loop enters past the clear (L1, 0x2b2), so popups age normally within a screen but
    // never carry across one.  Matching this stops a popup spawned inside a cave
    // from continuing to float on the surface after exit (was a port divergence
    // in both engines).  Zeroing the counter marks each slot inactive.
    for (auto& b : state.score_bonuses) {
        b.counter = 0;
        b.active_this_frame = false;
    }
}

}  // namespace olduvai::systems
