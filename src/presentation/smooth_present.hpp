// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Shared smooth-motion render-interpolation pacing — the general
// "fixed timestep + render interpolation" engine technique used by every
// enhanced render loop (surface, boss arenas, ending).
//
// Logic stays a fixed 18 Hz (EXE-faithful, deterministic).  Each logic tick's
// wall-time is then FILLED with interpolated render frames:
//   * vsync path: render at a CONTINUOUS alpha (elapsed / tick), paced by the
//     panel's vsync, so motion is smooth at the true refresh (60/120/144/VRR)
//     with no fixed-sub-frame quantisation and no 54-vs-60 Hz beat.  A carryover
//     accumulator pays back each tick's render-fill overshoot (an integer number
//     of vsync frames rarely divides the 55 ms tick — 3.3 refreshes at 60 Hz),
//     so the long-term logic cadence stays 18 Hz.
//   * fallback path: a fixed `discrete_n` evenly-spaced sub-frames with
//     SDL_Delay pacing — used when the driver refuses vsync.
//
// The caller supplies a render_at(alpha, sub) callback that interpolates its own
// fields to `alpha`, composes, presents, and restores the logic positions.  The
// helper owns only the alpha schedule, the vsync fill / discrete pacing, and the
// carryover.

#pragma once

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace olduvai::presentation {

// Refresh-adaptive sub-frame count for the discrete (no-vsync) fallback, and the
// nominal density hint.  Clamped [4,5]: 4 is already finer than the legacy 3 on
// a 60 Hz panel; 5 is the perf ceiling (worst-case HD compose ~9.6 ms ×5 = 48 ms
// fits the 55 ms tick).  OLDUVAI_SMOOTH_SUBFRAMES overrides for tuning.
inline int smooth_subframe_count(SDL_Window* win) {
    int refresh_hz = 60;
    SDL_DisplayMode dm;
    const int di = win != nullptr ? SDL_GetWindowDisplayIndex(win) : 0;
    if (di >= 0 && SDL_GetCurrentDisplayMode(di, &dm) == 0 &&
        dm.refresh_rate > 0) {
        refresh_hz = dm.refresh_rate;
    }
    int n = std::clamp(static_cast<int>(std::lround(refresh_hz / 18.0)), 4, 5);
    if (const char* ov = std::getenv("OLDUVAI_SMOOTH_SUBFRAMES")) {
        const int v = std::atoi(ov);
        if (v >= 1 && v <= 12) n = v;
    }
    return n;
}

// Request runtime vsync for the enhanced smooth-motion render-fill.  Returns
// true if the driver accepted it (SDL >= 2.0.18).  OLDUVAI_NO_VSYNC=1 forces
// the discrete fallback (for A/B testing).
inline bool smooth_try_enable_vsync(SDL_Renderer* ren, bool smooth) {
    if (!smooth || ren == nullptr || std::getenv("OLDUVAI_NO_VSYNC") != nullptr)
        return false;
    return SDL_RenderSetVSync(ren, 1) == 0;
}

// Per-loop pacing state.  `carryover` persists across ticks (declare it outside
// the frame loop, once per render loop).
struct SmoothPacer {
    bool vsync = false;
    int discrete_n = 4;
    Uint32 frame_ms = 1000 / 18;
    Uint32 carryover = 0;
};

// Fill one 18 Hz logic tick with interpolated render frames.  `render_at(alpha,
// sub)` must lerp its fields to alpha, compose, present, and restore the logic
// positions (it is called 1..N times).  Returns true if the vsync fill paced the
// tick — the caller must then SKIP its own frame delay (else it stacks and
// halves the rate).
template <class RenderAt>
inline bool smooth_fill_tick(SmoothPacer& p, RenderAt&& render_at) {
    if (p.vsync) {
        const Uint32 t0 = SDL_GetTicks();
        const Uint32 budget =
            p.frame_ms > p.carryover ? p.frame_ms - p.carryover : p.frame_ms;
        int sub = 0;
        while (true) {
            ++sub;
            const Uint32 el = SDL_GetTicks() - t0;
            const float alpha = el >= p.frame_ms
                                    ? 1.0f
                                    : static_cast<float>(el) /
                                          static_cast<float>(p.frame_ms);
            render_at(alpha, sub);
            const Uint32 e2 = SDL_GetTicks() - t0;
            if (e2 >= budget || sub >= 64) {
                p.carryover =
                    e2 > p.frame_ms
                        ? std::min(e2 - p.frame_ms, p.frame_ms)
                        : 0;
                return true;
            }
        }
    }
    for (int sub = 1; sub <= p.discrete_n; ++sub) {
        const Uint32 st = SDL_GetTicks();
        float alpha = static_cast<float>(sub) / static_cast<float>(p.discrete_n);
        if (alpha > 1.0f) alpha = 1.0f;
        render_at(alpha, sub);
        const Uint32 sub_ms = p.frame_ms / static_cast<Uint32>(p.discrete_n);
        const Uint32 spent = SDL_GetTicks() - st;
        if (sub < p.discrete_n && spent < sub_ms) SDL_Delay(sub_ms - spent);
    }
    return false;
}

}  // namespace olduvai::presentation
