// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "systems/secret.hpp"

#include "core/constants.hpp"

namespace olduvai::systems {

namespace {

struct SecretEntry {
    int screen, x_thresh, y_thresh, secret_idx, ret_screen, ret_x, ret_y;
};
constexpr SecretEntry kL1Secrets[] = {
    {5, 105, 155, 0, 5, 100, 70},
    {14, 105, 155, 1, 14, 80, 100},
};

void enter_secret(SystemsState& state, const SecretEntry& e) {
    state.secret_flag = 1;
    state.secret_index = e.secret_idx;
    state.secret_return_screen = e.ret_screen;
    state.secret_return_x = e.ret_x;
    state.secret_return_y = e.ret_y;
    state.current_screen = 100 + e.secret_idx;
    state.player.y = 10;   // drop in from the top
    state.screen_change = true;
}

}  // namespace

bool check_secret_entry(SystemsState& state) {
    if (state.cave_flag || state.secret_flag) return false;
    if (state.current_level != 1) return false;
    for (const auto& e : kL1Secrets) {
        if (state.current_screen != e.screen) continue;
        if (state.player.x < e.x_thresh && state.player.y > e.y_thresh) {
            enter_secret(state, e);
            return true;
        }
    }
    return false;
}

void check_secret_exit(SystemsState& state) {
    if (!state.secret_flag) return;
    PlayerState& p = state.player;
    if (!(p.y < 30 && p.gravity_flag != 0)) return;
    const int exiting_idx = state.secret_index;
    // Save the departure x BEFORE overwriting with the return position —
    // used by the enhanced-mode exit slide for the arc trajectory start.
    state.secret_exit_x = p.x;
    state.secret_flag = 0;
    state.current_screen = state.secret_return_screen;
    p.x = state.secret_return_x;
    p.y = state.secret_return_y;
    p.gravity_flag = 0;
    p.facing_left = 0;
    // Exit velocity dampening differs per room.
    if (exiting_idx == 0) {
        if ((p.y_vel & 0xFFFF) < 0x7F) p.y_vel >>= 2;
        else p.y_vel = 0x22;
    } else {
        p.y_vel >>= 2;
    }
    state.secret_index = -1;
    state.screen_change = true;
}

void setup_secret_collision(SystemsState& state) {
    state.collision.clear();
    for (int x = 0; x < core::kGameW; ++x) {
        state.collision.set_bit(x, kSecretFloorY);
    }
}

bool update_secret_trampoline(SystemsState& state) {
    if (!state.secret_flag) return false;
    PlayerState& p = state.player;
    if (p.x >= 20 || p.y != 139) return false;
    p.y -= 20;
    int new_vel = (p.y_vel << 2) & 0xFFFF;
    if (new_vel > 0x7F) new_vel = 0x7F;
    p.y_vel = new_vel;
    p.gravity_flag = 1;
    return true;
}

void update_flight_physics(SystemsState& state) {
    if (!state.glider_active) return;
    PlayerState& p = state.player;
    if (p.death_counter > 0) return;
    const int scr = state.current_screen;
    if (scr != 10 && scr != 11) return;
    p.y += 2;                                  // slow descent
    if (state.input.left && p.x > 4) p.x -= 5;
    if (state.input.right) p.x += 5;
    if (state.input.jump && p.y > 30) p.y -= 8;
    if (state.input.down) p.y += 5;
    p.restart_x = p.x;
    p.restart_y = 100;                          // hardcoded flight restart y
    p.restart_screen = scr;
    p.restart_glider = true;                    // respawn back ON the glider
}

void sync_balloon_visibility(SystemsState& state) {
    if (state.current_level != 1 && state.current_level != 5) return;
    for (core::Entity& e : state.entities) {
        if (e.obj_type == core::ObjType::Balloons) {
            e.visible = !state.glider_active;
        }
    }
}

}  // namespace olduvai::systems
