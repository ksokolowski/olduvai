// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OLDUVAI_FRAME_STATS: per-frame wall-clock accounting, shared by the platform
// and boss drivers.  Call begin_run() before the loop, begin_tick()/end_tick()
// around each tick's work, report() at the end.  Every method is a no-op when
// the env var is unset.  Read the field notes before interpreting a number.
#pragma once

#include <SDL.h>

#include <cstdint>
#include <vector>

namespace olduvai::presentation {

class TextOverlay;

struct FrameStats {
        std::uint64_t frames = 0, overruns = 0;
        double worst_ms = 0.0, worst_present_ms = 0.0, present_ms = 0.0;
        // Upscale time, read from enhance's own counter (upscale_rgba runs
        // before the present timer, from many sites).  `worst` is the upscale
        // of the worst-work frame, which can be a frame that barely upscaled;
        // `peak` is the worst upscale; `total` is for serial-vs-threaded
        // comparisons.
        double worst_upscale_ms = 0.0;    // upscale of the worst-WORK frame
        double peak_upscale_ms = 0.0;     // worst upscale of any frame
        double total_upscale_ms = 0.0;    // summed over the run
        unsigned long worst_upscale_calls = 0;
        // Same for present: the worst-work frame can be a paused menu that
        // presented nothing.  Use peak and total.
        double peak_present_ms = 0.0;     // worst present of any frame
        double total_present_ms = 0.0;    // summed over the run
        // SDL_RenderPresent alone.  With vsync it blocks until vblank (seconds
        // of "present" are the loop waiting), so real present work is present -
        // swap. Some devices run vsync=0 under opengles2, where the swap is
        // cheap.
        double swap_ms = 0.0;             // this frame, SDL_RenderPresent only
        double total_swap_ms = 0.0;
        // present_transition runs a whole animation in one main-loop iteration,
        // so per-frame present totals overstate.  `present_calls` counts
        // presents per frame, to divide by work done.  `upload_ms`:
        // SDL_UpdateTexture alone (widescreen scale 3 uploads 2.5 MB per
        // frame).
        unsigned long present_calls = 0;       // this frame
        unsigned long total_present_calls = 0;
        double upload_ms = 0.0;                // this frame, SDL_UpdateTexture
        double total_upload_ms = 0.0;
        // HUD text overlay (text_overlay.cpp, outside both presenters): memset,
        // upload and blended RenderCopy, each at output resolution.
        double ov_clear_ms = 0.0, ov_upload_ms = 0.0, ov_blit_ms = 0.0;
        double total_ov_clear_ms = 0.0, total_ov_upload_ms = 0.0,
               total_ov_blit_ms = 0.0;
        // Overlay skip-check cost, and how often it paid off (near-zero
        // ov_skipped = the hash is pure overhead).
        double ov_hash_ms = 0.0, total_ov_hash_ms = 0.0;
        // Dirty-rect uploads (LevelSurface::upload_dirty): presents that sent
        // only their rects, the upload calls those took, and the pixels sent.
        unsigned long dirty_partial = 0, total_dirty_partial = 0;
        unsigned long dirty_calls = 0, total_dirty_calls = 0;
        double dirty_mpx = 0.0, total_dirty_mpx = 0.0;
        double fg_ms = 0.0, total_fg_ms = 0.0;
        double bg_copy_ms = 0.0, total_bg_copy_ms = 0.0;
        double scene_ms = 0.0, total_scene_ms = 0.0;
        double glyph_ms = 0.0, total_glyph_ms = 0.0;
    // The driver's own pre-present compose (the arena into its framebuffer),
    // where the boss time goes: present cost was flat at ~23 ms/tick across
    // L2/L4/L6 on a TrimUI while eff_hz was 11.1 / 18.1 / 7.6.  `scene_ms` is
    // the separate pass into the wide buffer; widescreen runs both.
    double compose_ms = 0.0, total_compose_ms = 0.0;
        // Pacing.  `overruns` counts ticks over budget, and the smooth loop
        // fills the tick by design, so it rises when things improve.  What a
        // player feels:
        //   eff_hz    -- did the logic clock hold 18.2 Hz (below: slow game)
        //   late_*    -- how late the late ticks were
        //   subframes -- presents per tick (3.0 = 54 Hz target)
        Uint64 run_t0 = 0;
        double late_total_ms = 0.0, late_max_ms = 0.0;
        // Ticks that played a transition and their present time, excluded from
        // pacing (the sim is paused). Per-present intervals for percentiles and
        // the 1% low (means hide stutter). ~4 bytes per present.
        std::vector<float> present_iv_ms;
        Uint64 last_present_pc = 0;
        bool tick_paused = false;
        double paused_ms = 0.0;
        std::uint64_t paused_ticks = 0;
        unsigned long ov_skipped = 0, total_ov_skipped = 0;
        Uint64 t0 = 0;

    // ---- what a presenter calls -------------------------------------------
    // Presenters take one FrameStats* and record through Timer / note_present;
    // null-safe and cheap when disabled.

    // RAII: add this scope's wall time to one bucket.  `fs` may be null.
    class Timer {
      public:
        Timer(FrameStats* fs, double FrameStats::* field);
        ~Timer();
        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;
      private:
        double* accum_;
        double perf_ms_;
        Uint64 t0_;
    };

    // Count one present and record the interval since the last.
    void note_present();

    // ---- lifecycle -------------------------------------------------------
    // Reads the env var and the perf-counter scale.  Call once, before the
    // loop.
    void begin_run();

    // Clear this frame's buckets, reset enhance's upscale counters, stamp t0.
    // Call at the top of the loop body.
    void begin_tick();

    // Fold this frame into the totals and charge lateness or pause.  Call after
    // the frame's work, before the pacing wait (so `worst_work` excludes
    // sleep).
    void end_tick();

    // Start a new accounting phase (a boss victory after its fight): every
    // total back to zero, the run clock restarted.
    void begin_phase();

    // The two report lines (render-stats, then frame-stats) on stderr.
    // `phase` tags them ("L6 victory:"); null for the level itself.
    void report(int display_level, const char* phase = nullptr) const;

    // Whether this instance is recording.  Presenters take it as `stats_on`.
    bool enabled = false;
    // Milliseconds per performance-counter unit; presenters take it too.
    double perf_ms = 0.0;
    // One DosTicker period — the budget a tick is late against.
    double budget_ms = 1000.0 / 18.2065;
};

// Point the HUD text overlay's sinks at `stats` (after begin_run()).
void wire_overlay_stats(FrameStats& stats, TextOverlay& overlay);

}  // namespace olduvai::presentation
