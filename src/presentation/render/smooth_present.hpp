// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Smooth-motion pacing: fixed 18 Hz logic (EXE-faithful), each tick filled
// with interpolated render frames.
//   * vsync: frames at a continuous alpha (elapsed / tick), paced by the
//     panel, smooth at any refresh; a carryover repays each tick's overshoot
//     (60 Hz = 3.3 refreshes per 55 ms tick) so logic averages 18 Hz.
//   * fallback (vsync refused): `discrete_n` evenly spaced sub-frames with
//     SDL_Delay.
// The caller's render_at(alpha, sub) interpolates, composes, presents and
// restores; this owns the schedule, pacing and carryover.

#pragma once

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "presentation/render/smooth_config.hpp"

namespace olduvai::presentation {

// Sub-frames per tick for the discrete fallback (and the density hint): from
// the refresh rate, clamped [4,5] (5 x ~9.6 ms worst HD compose fits the 55 ms
// tick).  smooth_subframes / OLDUVAI_SMOOTH_SUBFRAMES override (env > config >
// derived).
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

// Was the count asked for (a ceiling) or derived (a hint)?  Capping the derived
// default on a 144 Hz panel cost fps 126 -> 87, 1% low 56 -> 29, jitter 1.45 ->
// 6.00 ms for 2.6% game speed.  Both fill loops use this one definition.
inline bool smooth_subframes_explicit() {
    return resolve_subframes_explicit(std::getenv("OLDUVAI_SMOOTH_SUBFRAMES"),
                                      smooth_present_config().subframes);
}

// Request runtime vsync for the smooth fill; true if the driver accepted it
// (SDL >= 2.0.18).  smooth_vsync=off or OLDUVAI_NO_VSYNC forces the fallback;
// the config knob is ignored under kmsdrm (resolve_vsync_off), with a log
// line.
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

// Fill one logic tick with interpolated frames; render_at(alpha, sub) is
// called 1..N times.  True if the vsync fill consumed the tick: the caller
// must then skip its own delay.
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
            // Only a met budget paced the tick.  Stopping early on the
            // sub-frame cap leaves time, and the caller must sleep it (else the
            // game runs fast: 20.52 Hz measured).
            const bool budget_met = e2 >= budget;
            if (budget_met ||
                (smooth_subframes_explicit() && sub >= p.discrete_n) ||
                sub >= 64) {
                p.carryover =
                    e2 > p.frame_ms
                        ? std::min(e2 - p.frame_ms, p.frame_ms)
                        : 0;
                return budget_met;
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
