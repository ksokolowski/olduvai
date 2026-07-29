// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/diag/reinit_test_hook.hpp"

#include <cstdio>
#include <vector>

#include "core/types.hpp"   // core::Entity

namespace olduvai::presentation {
namespace {

// Two latches + the pre-reinit snapshot carry state across the two
// run_platform_level calls (frame-5 trigger → kReinitDisplay → run_game
// rebuilds → re-entry with restore).  Process-global, zero-initialised, reset
// naturally because reinit_smoke.sh runs a fresh process per assertion.
bool s_triggered = false;      // have we fired the trigger?
bool s_done = false;           // have we written the result file?
int s_pre_x = 0, s_pre_y = 0;  // player pos before reinit
int s_pre_entcount = -1;       // live entity count before reinit
unsigned s_pre_entsum = 0;     // entity CONTENT checksum before reinit

// FNV-1a content checksum over the mutable per-entity fields: an equal COUNT of
// freshly-respawned (reset) entities must NOT pass the round-trip check — the
// exact false-confidence shape this hook was built to close.
unsigned ent_checksum(const std::vector<core::Entity>& es) {
    unsigned h = 2166136261u;
    const auto mix = [&h](int v) {
        h ^= static_cast<unsigned>(v);
        h *= 16777619u;
    };
    for (const auto& e : es) {
        mix(static_cast<int>(e.obj_type));
        mix(e.x);
        mix(e.y);
        mix(e.state);
        mix(e.counter);
        mix(e.ko_counter);
        mix(e.active ? 1 : 0);
    }
    return h;
}

}  // namespace

void ReinitTestHook::maybe_write_result(const systems::SystemsState& state,
                                        SDL_Window* win, bool& running) {
    if (path_ == nullptr || !(s_triggered && !s_done)) return;
    s_done = true;
    int out_w = 0, out_h = 0;
    // Logical window size (NOT renderer output size): DPI-independent, so the
    // assertion holds on macOS Retina where the output size is 2x the logical.
    SDL_GetWindowSize(win, &out_w, &out_h);
    const int post_x = state.player.x;
    const int post_y = state.player.y;
    const int post_entcount = static_cast<int>(state.entities.size());
    const unsigned post_entsum = ent_checksum(state.entities);
    // "wb": the file is machine-parsed by reinit_smoke.sh; text mode on Windows
    // would append \r to the last field and break the shell string compare.
    if (FILE* f = std::fopen(path_, "wb")) {
        std::fprintf(f, "%d %d %d %d %d %d %d %d %u %u\n", out_w, out_h, s_pre_x,
                     s_pre_y, post_x, post_y, s_pre_entcount, post_entcount,
                     s_pre_entsum, post_entsum);
        std::fclose(f);
    }
    running = false;
}

void ReinitTestHook::maybe_trigger(const systems::SystemsState& state,
                                   const GameOptions& opts, int frame,
                                   bool menu_ok, PendingReinit& reinit_req,
                                   bool& want_reinit, PauseService& pause) {
    // Frame 5: the player has a valid spawn position by then (GET READY counter
    // started at 0x11).
    if (path_ == nullptr || s_triggered || !menu_ok || frame != 5) return;
    s_triggered = true;
    s_pre_x = state.player.x;
    s_pre_y = state.player.y;
    s_pre_entcount = static_cast<int>(state.entities.size());
    s_pre_entsum = ent_checksum(state.entities);
    // Force the save→reinit→restore MECHANISM directly (decoupled from the Pause
    // classifier): seed the target fields + raise want_reinit so the pause block
    // captures the snapshot and returns kReinitDisplay.
    reinit_req.enhanced = opts.enhanced;
    reinit_req.render_scale = 4;
    reinit_req.hd_profile = opts.hd_profile;
    reinit_req.music_device = opts.music_device;
    reinit_req.sfx_backend = opts.sfx_backend;
    want_reinit = true;
    pause.set_open(true);
}

}  // namespace olduvai::presentation
