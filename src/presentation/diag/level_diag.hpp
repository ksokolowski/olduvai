// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The platform level's dev instrumentation: the OLDUVAI_* hooks
// (docs/internal/DEBUG_HOOKS.md), read once at level entry, the frame
// stats, the perf and pace traces, the draw log, the --debug-* overlays, and
// the F5 report writer.  None of it is part of the frame contract.
#pragma once

#include <climits>
#include <cstdio>
#include <functional>
#include <memory>

#include <SDL.h>

#include "presentation/diag/bug_capture.hpp"    // BugAnnotations
#include "presentation/diag/frame_stats.hpp"
#include "presentation/diag/menu_script.hpp"
#include "presentation/game_app.hpp"            // GameOptions
#include "presentation/level/level_state.hpp"   // Loaded
#include "presentation/render/game_render.hpp"  // FrameBuffer

namespace olduvai::presentation {

class PauseService;
class WidescreenPresenter;
class TickPacer;

// The level's OLDUVAI_* hooks.  Unset or malformed = off / never.
struct LevelHooks {
    int force_level_complete = -1;   // OLDUVAI_FORCE_LEVEL_COMPLETE=<frame>
    int smooth_frames = INT_MAX;     // OLDUVAI_SMOOTH_FRAMES=<n>
    bool bubble_trace = false;       // OLDUVAI_BUBBLE_TRACE
    bool pace_trace = false;         // OLDUVAI_PACE_TRACE
    bool perf_log = false;           // OLDUVAI_PERF_LOG
    const char* pause_shot = nullptr;     // OLDUVAI_PAUSE_SHOT=<path>
    const char* pause_screen = nullptr;   // OLDUVAI_PAUSE_SCREEN=<id>
    const char* draw_log = nullptr;       // OLDUVAI_DRAW_LOG=<file>

    static LevelHooks from_env();
};

struct LevelDiag {
    LevelDiag();

    LevelHooks hooks = LevelHooks::from_env();
    FrameStats stats;
    // --debug-perf overlay: smoothed fps / frame-ms drawn into the frame.
    struct PerfOverlay {
        Uint32 last_t = 0, interval_accum = 0, ms_accum = 0;
        int samples = 0;
        double fps = 0.0, frame_ms = 0.0;
    } perf;
    // OLDUVAI_MENU_SCRIPT: headless menu walk (tests/menu_script.sh,
    // tests/report_form.sh) — diag/menu_script.hpp.
    MenuScript menu;
    // OLDUVAI_DRAW_LOG: every rendered frame's draw positions (player and
    // entities, sub-frames included) as JSONL, for offline outlier analysis.
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> draw_log{nullptr,
                                                             &std::fclose};

    // The --debug-perf sample, once per frame (F3 stats, OLDUVAI_PERF_LOG).
    void sample_perf(bool any_debug_overlay);
    // OLDUVAI_PACE_TRACE: the interval between presents (sub 1 is the tick
    // boundary).
    void trace_pace(int sub) const;
    // OLDUVAI_PACE_TRACE: the --vga-scan hold-frame rate over the level.
    void report_vga_pace(const TickPacer& pacer) const;
};

// --debug-collision / --debug-entities / --debug-perf, drawn just before the
// present.
void draw_debug_overlays(FrameBuffer& target, const GameOptions& opts,
                         const Loaded& g, const LevelDiag& diag, int hd_scale);

// OLDUVAI_PAUSE_SHOT: open Pause on frame 1 (OLDUVAI_PAUSE_SCREEN, default
// "pause") and dump it.
void open_pause_shot(PauseService& pause, bool menu_ok,
                     const LevelHooks& hooks);

}  // namespace olduvai::presentation
