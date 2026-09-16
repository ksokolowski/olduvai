// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OLDUVAI_FRAME_STATS — per-frame wall-clock budget accounting, shared by the
// two level drivers.
//
// WHY IT IS HERE AND NOT IN game_app.cpp, where it grew.  There are two frame
// loops in this tree — `run_platform_level` (levels 1,3,5,7) and
// `run_boss_level` (2,4,6) — and every counter below was added to the first
// one only.  The whole September 2026 handheld optimisation pass was therefore
// measured on four of the seven levels, and the one pacing defect that WAS
// found on the boss side (`c886259`: the sub-frame cap reached one of the two
// fill loops) was found by reading, not by measuring, because the boss driver
// had nothing to measure with.
//
// So the contract is: the counters, the per-tick bookkeeping and the report
// format live here ONCE.  A driver owns an instance, calls begin_run() before
// its loop, begin_tick()/end_tick() around each tick's work, and report() at
// the end.  Every method is a no-op when OLDUVAI_FRAME_STATS is unset, so an
// uninstrumented build path costs one bool test per tick.
//
// What the individual numbers mean, and the three separate misreadings that
// produced them, is documented field by field below — those comments are the
// expensive part of this file.
#pragma once

#include <SDL.h>

#include <cstdint>
#include <vector>

namespace olduvai::presentation {

struct FrameStats {
        std::uint64_t frames = 0, overruns = 0;
        double worst_ms = 0.0, worst_present_ms = 0.0, present_ms = 0.0;
        // Upscale cost of the WORST frame.  Read from enhance's own
        // counter rather than timed here: upscale_rgba has 19 call sites
        // across 10 files and runs BEFORE the present timer, so the most
        // expensive work in HD was invisible until 2026-09-06.
        // THREE numbers, because one was ambiguous and produced a nonsense
        // comparison: `worst_upscale_ms` is the upscale of THE WORST FRAME,
        // which for a cheap profile can be a frame that barely upscaled at all
        // (mmpx once read 0.07 ms against a true 5 ms).  `peak` is the worst
        // upscale in its own right, and `total` is what a serial-vs-threaded
        // comparison actually wants.
        double worst_upscale_ms = 0.0;    // upscale of the worst-WORK frame
        double peak_upscale_ms = 0.0;     // worst upscale of any frame
        double total_upscale_ms = 0.0;    // summed over the run
        unsigned long worst_upscale_calls = 0;
        // PRESENT HAS THE SAME TRAP, and it went unnoticed for longer because
        // the upscale fix above was not carried across to it.  `present=0.00ms`
        // was read off a device run and taken to mean "SDL costs nothing" — it
        // meant the worst-WORK frame was a paused menu (wall-clock artifact,
        // ~1993 ms) which presented nothing at all.  Peak and total are the
        // numbers that answer "is the SDL present path worth optimising".
        double peak_present_ms = 0.0;     // worst present of any frame
        double total_present_ms = 0.0;    // summed over the run
        // AND THE SWAP SPLIT OUT OF IT, because the whole-present figure is
        // not a cost.  With vsync=1 SDL_RenderPresent BLOCKS
        // until the vertical blank (NOT universal: the same device reported
        // vsync=0 under opengles2 on 2026-09-09, where the swap is cheap and
        // this split buys accounting rather than a big number).  The first
        // device run to report present at
        // all read 70,299 ms over 974 frames (~72 ms/frame) with a 1899.94 ms
        // peak — no GPU takes 1.9 s to swap, that is the loop waiting.  Real
        // work is (present - swap): UpdateTexture + Clear + Copy.  Reporting
        // the sum as "what SDL costs" would have been the third wrong reading
        // of this same column.
        double swap_ms = 0.0;             // this frame, SDL_RenderPresent only
        double total_swap_ms = 0.0;
        // AND THE REMAINDER IS STILL NOT A PER-FRAME COST.  Measured on the
        // device 2026-09-09: present_total 20,243 ms over 495 frames — 41 ms a
        // frame — while only FIVE frames overran.  Both cannot be true, and
        // they reconcile one way: present_transition runs a whole transition
        // animation inside ONE main-loop iteration, so an entire animation's
        // duration lands in a single frame's present_ms (present_peak was
        // 1213 ms).  The seconds are real; the label is wrong.
        //
        // These two divide it.  `present_calls` is how many present/
        // present_transition invocations a frame actually made, so the total
        // can be divided by work done rather than by main-loop frames.
        // `upload_ms` is SDL_UpdateTexture alone — the leading suspect, since
        // scale-3 widescreen uploads 1068x600x4 = 2.5 MB per animation frame
        // and upscale_total was only 388 ms of the whole session, which rules
        // the scaler and the upscaler out entirely.
        unsigned long present_calls = 0;       // this frame
        unsigned long total_present_calls = 0;
        double upload_ms = 0.0;                // this frame, SDL_UpdateTexture
        double total_upload_ms = 0.0;
        // The HUD text overlay, which the first upload instrument MISSED: it
        // lives in text_overlay.cpp, not in either presenter, so upload_ms
        // above undercounts total texture traffic.  Three buckets because the
        // suspected cost is three different things at output resolution -- a
        // full-buffer memset, a full-buffer upload, and a blended full-screen
        // RenderCopy -- and only one of them has an obvious fix.
        double ov_clear_ms = 0.0, ov_upload_ms = 0.0, ov_blit_ms = 0.0;
        double total_ov_clear_ms = 0.0, total_ov_upload_ms = 0.0,
               total_ov_blit_ms = 0.0;
        // The skip check and how often it paid off.  ov_skipped is the number
        // that says whether the cache is working at all: if it is near zero the
        // overlay genuinely changes every frame and the hash is pure overhead.
        double ov_hash_ms = 0.0, total_ov_hash_ms = 0.0;
        double fg_ms = 0.0, total_fg_ms = 0.0;
        double bg_copy_ms = 0.0, total_bg_copy_ms = 0.0;
        double scene_ms = 0.0, total_scene_ms = 0.0;
        double glyph_ms = 0.0, total_glyph_ms = 0.0;
    // THE DRIVER'S OWN PRE-PRESENT COMPOSE, which on the boss side is where
    // the time actually went.  Measured 2026-09-10 on a TrimUI: present cost
    // was FLAT at ~23 ms/tick across L2/L4/L6 while the levels ran at 11.1,
    // 18.1 and 7.6 eff_hz — so none of the 4:2:6 spread the owner felt by eye
    // was in the present path, and every counter that existed was pointed at
    // the present path.
    //
    // Separate from `scene_ms` because they are different passes, not two
    // names for one: `compose_ms` is the arena drawn into the driver's own
    // framebuffer, `scene_ms` is the fight sprites drawn into the WIDE buffer
    // that actually gets uploaded.  On the widescreen path both run, every
    // sub-frame.  Whether the first is then discarded is what these two
    // numbers are here to establish rather than assume.
    double compose_ms = 0.0, total_compose_ms = 0.0;
        // PACING, because `overruns` stopped answering the question.  It counts
        // ticks over budget, and the smooth loop deliberately fills the tick:
        // at 13.6 ms a present, four fit in 54.4 ms of a 54.9 ms tick and a
        // fifth overshoots slightly.  So the count ROSE 354 -> 472 across a
        // change where worst_work and present_peak both FELL.  It measures
        // crossings, not magnitude, and a loop working as designed maximises
        // crossings.
        //
        // These answer what a player actually feels:
        //   eff_hz    -- did the LOGIC CLOCK hold 18.2 Hz?  Below it, the game
        //                literally runs slow; that is the only unambiguous
        //                failure, and no amount of present tuning hides it.
        //   late_*    -- by HOW MUCH the late ticks were late, not how many.
        //   subframes -- presents per logic tick.  3.0 is the smooth-motion
        //                target (54 Hz / 18.2 Hz); more is wasted work, fewer
        //                is visible stepping.
        Uint64 run_t0 = 0;
        double late_total_ms = 0.0, late_max_ms = 0.0;
        // Ticks in which a transition played, and the present time they spent
        // doing it.  Both are removed from the pacing figures: the sim is
        // paused for a transition, so its wall time is animation, not slippage.
        // PER-PRESENT INTERVALS, the basis of every standard smoothness
        // figure.  Averages hide stutter by construction -- a run that drops
        // one frame in twenty reports a fine mean FPS and looks bad -- so the
        // percentiles and the 1% low are what actually correspond to what a
        // player sees.  ~4 bytes a present; a 1200-frame run at 4 sub-frames
        // is under 20 kB.
        std::vector<float> present_iv_ms;
        Uint64 last_present_pc = 0;
        bool tick_paused = false;
        double paused_ms = 0.0;
        std::uint64_t paused_ticks = 0;
        unsigned long ov_skipped = 0, total_ov_skipped = 0;
        Uint64 t0 = 0;

    // ---- what a presenter calls -------------------------------------------
    // ONE POINTER, NOT NINE.  FramePresenter and WidescreenPresenter each
    // carry a loose sink per bucket (`double* present_ms`, `Uint64*
    // last_present_pc`, `bool stats_on`, …) because the counters had no type
    // to belong to.  They do now, so BossArenaPresenter takes a `FrameStats*`
    // instead and a third copy of that field list never gets written.  The
    // two platform presenters can be converted the same way whenever someone
    // is in there anyway; nothing here forces it.
    //
    // Both are null-safe on the FrameStats side and cheap when disabled, so an
    // unwired or unmeasured presenter pays a pointer test.

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

    // One present happened: count it and record the interval since the last,
    // which is what every figure in the render-stats line is computed from.
    void note_present();

    // ---- lifecycle -------------------------------------------------------
    // Reads the env gate and the perf-counter scale.  Call once, before the
    // loop; everything below is a no-op until it has run and found the var.
    void begin_run();

    // Per-tick reset — clears the this-frame buckets, resets enhance's
    // upscale counters and stamps t0.  Call at the TOP of the loop body.
    void begin_tick();

    // Per-tick accumulate — folds this frame's buckets into the totals and
    // charges the tick to lateness or to `paused`.  Call AFTER the frame's
    // work and BEFORE the pacing wait: the intentional sleep is deliberately
    // outside the measured window, which is what makes `worst_work` mean work.
    void end_tick();

    // The two report lines (render-stats, then frame-stats) on stderr.
    void report(int display_level) const;

    // Whether this instance is recording.  Presenters take it as `stats_on`.
    bool enabled = false;
    // Milliseconds per performance-counter unit; presenters take it too.
    double perf_ms = 0.0;
    // One DosTicker period — the budget a tick is late against.
    double budget_ms = 1000.0 / 18.2065;
};

}  // namespace olduvai::presentation
