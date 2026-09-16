// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// FrameStats bookkeeping — moved out of game_app.cpp verbatim so the boss
// driver can record the same numbers in the same format.  See the header for
// why that mattered enough to move.
#include "presentation/diag/frame_stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "enhance/upscale.hpp"

namespace olduvai::presentation {

// OLDUVAI_FRAME_STATS: per-frame timing health — the headless twin of
// OLDUVAI_AUDIO_STATS.  Measures a frame's WORK (loop top -> just before the
// pacing wait; the intentional sleep is excluded) at perf-counter
// resolution, tracking the worst frame vs the ~55 ms tick budget, how many
// frames blew it, and the present/upload phase within that worst frame.
// Lets slow-HW frame-budget violations be diagnosed without a display;
// near-zero cost when the env var is unset.
FrameStats::Timer::Timer(FrameStats* fs, double FrameStats::* field)
    : accum_((fs != nullptr && fs->enabled) ? &(fs->*field) : nullptr),
      perf_ms_(fs != nullptr ? fs->perf_ms : 0.0),
      t0_(accum_ != nullptr ? SDL_GetPerformanceCounter() : 0) {}

FrameStats::Timer::~Timer() {
    if (accum_ != nullptr)
        *accum_ += static_cast<double>(SDL_GetPerformanceCounter() - t0_) *
                   perf_ms_;
}

void FrameStats::note_present() {
    if (!enabled) return;
    ++present_calls;
    const Uint64 now_pc = SDL_GetPerformanceCounter();
    // The cap is a memory guard, not a sampling window: 200k intervals is a
    // ~4 hour run at 60 Hz, and dropping the tail is better than growing the
    // vector without bound in a session someone left running.
    if (last_present_pc != 0 && present_iv_ms.size() < 200000)
        present_iv_ms.push_back(static_cast<float>(
            static_cast<double>(now_pc - last_present_pc) * perf_ms));
    last_present_pc = now_pc;
}

void FrameStats::begin_run() {
    enabled = std::getenv("OLDUVAI_FRAME_STATS") != nullptr;
    perf_ms = 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}

void FrameStats::begin_tick() {
    if (!enabled) return;
    present_ms = 0.0;
    tick_paused = false;
    swap_ms = 0.0;
    upload_ms = 0.0;
    present_calls = 0;
    ov_clear_ms = 0.0;
    ov_upload_ms = 0.0;
    ov_blit_ms = 0.0;
    ov_hash_ms = 0.0;
    fg_ms = 0.0;
    bg_copy_ms = 0.0;
    scene_ms = 0.0;
    glyph_ms = 0.0;
    compose_ms = 0.0;
    ov_skipped = 0;
    enhance::reset_upscale_stats();
    t0 = SDL_GetPerformanceCounter();
    if (run_t0 == 0) run_t0 = t0;
}

void FrameStats::end_tick() {
    if (!enabled) return;
    const double work_ms =
        static_cast<double>(SDL_GetPerformanceCounter() - t0) *
        perf_ms;
    ++frames;
    if (work_ms > worst_ms) {
        worst_ms = work_ms;
        worst_present_ms = present_ms;  // present of the worst frame
        const auto us = enhance::upscale_stats();
        worst_upscale_ms = us.ms;
        worst_upscale_calls = us.calls;
    }
    if (tick_paused) {
        // A transition tick: charge its present time to `paused`
        // rather than to lateness.
        ++paused_ticks;
        paused_ms += present_ms;
    } else if (work_ms > budget_ms) {
        ++overruns;
        const double late = work_ms - budget_ms;
        late_total_ms += late;
        if (late > late_max_ms)
            late_max_ms = late;
    }
    const auto uf = enhance::upscale_stats();
    total_upscale_ms += uf.ms;
    if (uf.ms > peak_upscale_ms)
        peak_upscale_ms = uf.ms;
    total_present_ms += present_ms;
    if (present_ms > peak_present_ms)
        peak_present_ms = present_ms;
    total_swap_ms += swap_ms;
    total_upload_ms += upload_ms;
    total_ov_clear_ms += ov_clear_ms;
    total_ov_upload_ms += ov_upload_ms;
    total_ov_blit_ms += ov_blit_ms;
    total_ov_hash_ms += ov_hash_ms;
    total_fg_ms += fg_ms;
    total_bg_copy_ms += bg_copy_ms;
    total_scene_ms += scene_ms;
    total_glyph_ms += glyph_ms;
    total_compose_ms += compose_ms;
    total_ov_skipped += ov_skipped;
    total_present_calls += present_calls;
}

void FrameStats::report(int display_level) const {
    if (!enabled) return;
    if (present_iv_ms.size() >= 20) {
        // The standard game-performance set, computed from the per-present
        // intervals rather than from an average.
        //
        // WHY THE LOWS AND NOT THE MEAN.  Mean FPS is the figure that hides
        // stutter: a run that drops one present in twenty still reports a
        // healthy average and still looks bad.  The 1% low -- the mean of the
        // slowest 1% of intervals, expressed as a rate -- is the industry's
        // answer to that, and it is what corresponds to what a player notices.
        //
        // JITTER is separate from both.  Evenly-spaced 30 FPS reads as smooth;
        // 60 FPS alternating 8 ms and 25 ms reads as judder at twice the frame
        // rate.  Mean absolute deviation of the intervals catches that, and
        // nothing else in this block does.
        std::vector<float> iv = present_iv_ms;
        std::sort(iv.begin(), iv.end());
        const std::size_t n = iv.size();
        const auto pct = [&](double q) {
            std::size_t i = static_cast<std::size_t>(q * (n - 1));
            return static_cast<double>(iv[i]);
        };
        double sum = 0.0;
        for (const float v : iv) sum += v;
        const double mean = sum / static_cast<double>(n);
        // Slowest 1% and 0.1%, averaged, as rates.
        const auto low_rate = [&](double frac) {
            const std::size_t k =
                std::max<std::size_t>(1, static_cast<std::size_t>(n * frac));
            double acc = 0.0;
            for (std::size_t i = n - k; i < n; ++i) acc += iv[i];
            const double avg = acc / static_cast<double>(k);
            return avg > 0.0 ? 1000.0 / avg : 0.0;
        };
        double mad = 0.0;
        for (const float v : iv) mad += std::fabs(static_cast<double>(v) - mean);
        mad /= static_cast<double>(n);
        std::fprintf(stderr,
                     "render-stats L%d: fps=%.2f (1%%low=%.2f 0.1%%low=%.2f) "
                     "frame_ms p50=%.2f p95=%.2f p99=%.2f max=%.2f "
                     "jitter_mad=%.2fms samples=%zu\n",
                     display_level, mean > 0.0 ? 1000.0 / mean : 0.0,
                     low_rate(0.01), low_rate(0.001),
                     pct(0.50), pct(0.95), pct(0.99),
                     static_cast<double>(iv[n - 1]), mad, n);
    }
    if (frames == 0) return;
    std::fprintf(stderr,
                 "frame-stats L%d: frames=%llu overruns=%llu(>%.1fms) "
                 "worst_work=%.2fms (present=%.2fms upscale=%.2fms/%luc) "
                 "upscale_peak=%.2fms upscale_total=%.1fms "
                 "present_peak=%.2fms present_total=%.1fms "
                 "swap_total=%.1fms present_work=%.1fms "
                 "upload_total=%.1fms present_calls=%lu "
                 "ov_clear=%.1fms ov_upload=%.1fms ov_blit=%.1fms "
                 "ov_hash=%.1fms ov_skipped=%lu fg=%.1fms bg_copy=%.1fms scene=%.1fms glyph=%.1fms "
                 "compose=%.1fms "
                 "| PACING eff_hz=%.2f late_total=%.1fms late_max=%.1fms "
                 "subframes=%.2f paused=%.1fms/%lluticks "
                 "budget=%.2fms\n",
                 display_level,
                 static_cast<unsigned long long>(frames),
                 static_cast<unsigned long long>(overruns), budget_ms,
                 worst_ms, worst_present_ms,
                 worst_upscale_ms, worst_upscale_calls,
                 peak_upscale_ms, total_upscale_ms,
                 peak_present_ms, total_present_ms,
                 total_swap_ms,
                 total_present_ms - total_swap_ms,
                 total_upload_ms,
                 total_present_calls,
                 total_ov_clear_ms,
                 total_ov_upload_ms,
                 total_ov_blit_ms,
                 total_ov_hash_ms,
                 total_ov_skipped,
                 total_fg_ms,
                 total_bg_copy_ms,
                 total_scene_ms,
                 total_glyph_ms,
                 total_compose_ms,
                 // eff_hz: the ONLY unambiguous failure signal here.  If
                 // the logic clock is under 18.2 Hz the game literally runs
                 // slow, and no present-side tuning disguises it.
                 // Wall time MINUS the transition animations, so this is
                 // the rate the LOGIC actually held.
                 (run_t0 != 0 && SDL_GetPerformanceCounter() >
                      run_t0)
                     ? static_cast<double>(frames) * 1000.0 /
                       std::max(1.0,
                         static_cast<double>(SDL_GetPerformanceCounter() -
                                             run_t0) * perf_ms
                         - paused_ms)
                     : 0.0,
                 late_total_ms,
                 late_max_ms,
                 static_cast<double>(total_present_calls) /
                     static_cast<double>(frames),
                 paused_ms,
                 static_cast<unsigned long long>(paused_ticks),
                 budget_ms);
}

}  // namespace olduvai::presentation
