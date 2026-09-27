// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/diag/level_diag.hpp"

#include <cstdlib>
#include <string>

#include "presentation/diag/debug_overlay.hpp"
#include "presentation/env_num.hpp"                 // env_int
#include "presentation/menu/pause_service.hpp"
#include "presentation/window_util.hpp"                 // TickPacer

namespace olduvai::presentation {

LevelHooks LevelHooks::from_env() {
    LevelHooks h;
    h.force_level_complete = env_int("OLDUVAI_FORCE_LEVEL_COMPLETE", -1);
    h.smooth_frames = env_int("OLDUVAI_SMOOTH_FRAMES", INT_MAX);
    h.bubble_trace = std::getenv("OLDUVAI_BUBBLE_TRACE") != nullptr;
    h.pace_trace = std::getenv("OLDUVAI_PACE_TRACE") != nullptr;
    h.perf_log = std::getenv("OLDUVAI_PERF_LOG") != nullptr;
    h.pause_shot = std::getenv("OLDUVAI_PAUSE_SHOT");
    h.pause_screen = std::getenv("OLDUVAI_PAUSE_SCREEN");
    h.draw_log = std::getenv("OLDUVAI_DRAW_LOG");
    return h;
}

LevelDiag::LevelDiag() {
    if (hooks.draw_log != nullptr) draw_log.reset(std::fopen(hooks.draw_log, "w"));
}

void LevelDiag::sample_perf(bool any_debug_overlay) {
    if (!any_debug_overlay) return;
    const Uint32 now = SDL_GetTicks();
    perf.interval_accum += now - perf.last_t;
    perf.last_t = now;
    ++perf.samples;
    if (perf.samples >= 30) {
        const double avg_interval =
            static_cast<double>(perf.interval_accum) / perf.samples;
        perf.fps = avg_interval > 0.0 ? 1000.0 / avg_interval : 0.0;
        perf.frame_ms =
            static_cast<double>(perf.ms_accum) / perf.samples;
        if (hooks.perf_log)
            std::fprintf(stderr, "[PERF] frame_ms=%.2f fps=%.1f\n",
                         perf.frame_ms, perf.fps);
        perf.interval_accum = 0;
        perf.ms_accum = 0;
        perf.samples = 0;
    }
}

void LevelDiag::trace_pace(int sub) const {
    if (!hooks.pace_trace) return;
    static Uint32 last_present = 0;
    const Uint32 now = SDL_GetTicks();
    std::fprintf(stderr, "[PACE] sub=%d dt=%u\n", sub,
                 last_present ? now - last_present : 0);
    last_present = now;
}

void LevelDiag::report_vga_pace(const TickPacer& pacer) const {
    if (pacer.fill_ticks() == 0 || !hooks.pace_trace) return;
    std::fprintf(stderr, "[PACE] vga-scan: %.2f presents/tick over %lu ticks\n",
                 static_cast<double>(pacer.fill_presents()) /
                     static_cast<double>(pacer.fill_ticks()),
                 pacer.fill_ticks());
}

void draw_debug_overlays(FrameBuffer& target, const GameOptions& opts,
                         const Loaded& g, const LevelDiag& diag,
                         int hd_scale) {
    if (opts.debug_collision) draw_debug_collision(target, g.state, hd_scale);
    if (opts.debug_entities)
        draw_debug_entities(target, g.state, g.render.entity_sprites,
                            hd_scale);
    if (opts.debug_perf)
        draw_debug_perf(target, g.charset, g.render.palette, diag.perf.fps,
                        diag.perf.frame_ms, hd_scale);
}

void open_pause_shot(PauseService& pause, bool menu_ok,
                     const LevelHooks& hooks) {
    if (hooks.pause_shot == nullptr || !menu_ok) return;
    pause.force_open_screen(hooks.pause_screen != nullptr ? hooks.pause_screen
                                                          : "pause");
}

}  // namespace olduvai::presentation
