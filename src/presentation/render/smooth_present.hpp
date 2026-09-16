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
#include <cstdio>
#include <cstdlib>

#include "presentation/render/smooth_config.hpp"

namespace olduvai::presentation {

// Refresh-adaptive sub-frame count for the discrete (no-vsync) fallback, and the
// nominal density hint.  Clamped [4,5]: 4 is already finer than the legacy 3 on
// a 60 Hz panel; 5 is the perf ceiling (worst-case HD compose ~9.6 ms x5 = 48 ms
// fits the 55 ms tick).  The smooth_subframes key or OLDUVAI_SMOOTH_SUBFRAMES
// overrides (smooth_config.hpp: env > config > derived).
inline int smooth_subframe_count(SDL_Window* win) {
    int refresh_hz = 60;
    SDL_DisplayMode dm;
    const int di = win != nullptr ? SDL_GetWindowDisplayIndex(win) : 0;
    if (di >= 0 && SDL_GetCurrentDisplayMode(di, &dm) == 0 &&
        dm.refresh_rate > 0) {
        refresh_hz = dm.refresh_rate;
    }
    const int derived =
        std::clamp(static_cast<int>(std::lround(refresh_hz / 18.0)), 4, 5);
    return resolve_subframe_count(std::getenv("OLDUVAI_SMOOTH_SUBFRAMES"),
                                  smooth_present_config().subframes, derived);
}

// Was the sub-frame count ASKED FOR, or derived from the refresh rate?
//
// It decides whether the count is a CEILING or merely a hint, and the two fill
// loops must agree.  Capping the refresh-derived default is a regression:
// measured on a 144 Hz desktop, capping at smooth_N=5 gave five presents at
// vblank rate then a ~20 ms timer wait -- fps 126->87, 1%low 56->29, jitter
// 1.45->6.00 ms, for 2.6% of game speed.  So only an explicit request caps.
//
// ONE definition rather than one per loop: run_platform_level keeps an inline
// twin of smooth_fill_tick, and the first version of this fix reached only the
// inline one -- leaving the boss driver, which uses the helper, still ignoring
// the knob.  That is BACKLOG.md §1's failure mode (one concept, several bodies,
// one of them fixed) committed by the change that was cleaning up another
// instance of it.
inline bool smooth_subframes_explicit() {
    return resolve_subframes_explicit(std::getenv("OLDUVAI_SMOOTH_SUBFRAMES"),
                                      smooth_present_config().subframes);
}

// Request runtime vsync for the enhanced smooth-motion render-fill.  Returns
// true if the driver accepted it (SDL >= 2.0.18).  smooth_vsync=off (config)
// or OLDUVAI_NO_VSYNC (env, any value) forces the discrete fallback — except
// that the config knob does not apply under kmsdrm (resolve_vsync_off says
// why), and says so once in the log rather than silently.
inline bool smooth_try_enable_vsync(SDL_Renderer* ren, bool smooth) {
    if (!smooth || ren == nullptr) return false;
    const char* env = std::getenv("OLDUVAI_NO_VSYNC");
    const char* driver = SDL_GetCurrentVideoDriver();
    const bool config_off = smooth_present_config().vsync_off;
    if (resolve_vsync_off(env, config_off, driver)) return false;
    if (config_off && env == nullptr) {
        static bool told = false;
        if (!told) {
            std::fprintf(stderr, "smooth: smooth_vsync=off ignored under "
                                 "kmsdrm (it would mean async page flips, "
                                 "which this driver rejects); vsync on\n");
            told = true;
        }
    }
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
            if (e2 >= budget ||
                (smooth_subframes_explicit() && sub >= p.discrete_n) ||
                sub >= 64) {
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
