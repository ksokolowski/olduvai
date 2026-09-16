// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Smooth-motion present tuning as CONFIG — the keys smooth_subframes and
// smooth_vsync — plus the rule the pacer resolves them by:
//   env var  >  config key  >  refresh-derived default.
// The env vars (OLDUVAI_SMOOTH_SUBFRAMES, OLDUVAI_NO_VSYNC) stay as debug
// overrides with their old behaviour; a profile, play.json or a launcher now
// carries the shipped value (docs/internal/specs/2026-09-13-profile-families-
// design.md, section 3).
//
// SDL-free, split from smooth_present.hpp on purpose: app/options_build.cpp
// (compiled into the SDL-free unit tier) refreshes this from the Options
// menu's persist hook, and tests/test_smooth_config.cpp pins the rule.

#pragma once

#include <cstdlib>
#include <string>

#include "presentation/env_num.hpp"   // parse_int

namespace olduvai::presentation {

struct SmoothPresentConfig {
    int subframes = 0;        // 0 = auto (refresh-derived); 1..12 = explicit
    bool vsync_off = false;   // smooth_vsync=off: force the discrete path
};

// The process-wide value the pacing sites read at every frame-loop start.
// run_game publishes it from GameOptions; the Options menu's persist hook
// refreshes it, so the rebuild an Enhanced switch triggers already paces with
// the new values rather than waiting for the next launch.
inline SmoothPresentConfig& smooth_present_config() {
    static SmoothPresentConfig cfg;
    return cfg;
}

// "0".."12".  False (and `out` untouched) for anything else.
inline bool parse_smooth_subframes(const std::string& text, int& out) {
    int v = 0;
    if (!parse_int(text, v) || v < 0 || v > 12) return false;
    out = v;
    return true;
}

// "auto" | "off".  False (and `off` untouched) for anything else.
inline bool parse_smooth_vsync(const std::string& text, bool& off) {
    if (text == "auto") {
        off = false;
        return true;
    }
    if (text == "off") {
        off = true;
        return true;
    }
    return false;
}

// Fold one persisted (key, value) into `c`.  True when the key is one of
// ours, whether or not the value parsed — an invalid value keeps `c`.
inline bool apply_smooth_key(SmoothPresentConfig& c, const std::string& key,
                             const std::string& value) {
    if (key == "smooth_subframes") {
        parse_smooth_subframes(value, c.subframes);
        return true;
    }
    if (key == "smooth_vsync") {
        parse_smooth_vsync(value, c.vsync_off);
        return true;
    }
    return false;
}

// Sub-frame count.  When the env var is SET it governs exactly as it did
// before config existed: a value in [1,12] wins, anything else keeps
// `derived`.  Unset: a config count in [1,12] wins; 0 means `derived`.
inline int resolve_subframe_count(const char* env, int config, int derived) {
    if (env != nullptr) {
        // atoi is safe HERE because the range check is the validation:
        // garbage parses to 0, 0 is outside [1,12], so a typo keeps the
        // computed default instead of becoming one.
        // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
        const int v = std::atoi(env);
        return (v >= 1 && v <= 12) ? v : derived;
    }
    return (config >= 1 && config <= 12) ? config : derived;
}

// Was the count ASKED FOR (a ceiling on the vsync fill) or derived (a hint)?
// Only an asked-for count may cap: capping the derived one was measured as a
// regression on a 144 Hz panel (smooth_present.hpp, commit d62d922).  A set
// env var counts as asked-for even when its value is invalid — unchanged.
inline bool resolve_subframes_explicit(const char* env, int config) {
    return env != nullptr || (config >= 1 && config <= 12);
}

// Force the discrete fallback?  Any OLDUVAI_NO_VSYNC value does, as before.
// The CONFIG knob does not apply under SDL's `kmsdrm` video driver: there,
// vsync off means asynchronous page flips, and a driver that cannot do them
// rejects every one — "Could not queue pageflip: -22" on each present
// (Powkiddy A12 / RK3128, 2026-09-13: 1747 errors in 260 ticks with off, none
// with auto, panel verified on for both runs).  The TrimUI's `mali` driver is
// where off was measured best, and it keeps it.  `video_driver` is
// SDL_GetCurrentVideoDriver(), null before video is up.
inline bool resolve_vsync_off(const char* env, bool config_off,
                              const char* video_driver = nullptr) {
    if (env != nullptr) return true;
    if (config_off && video_driver != nullptr &&
        std::string(video_driver) == "kmsdrm")
        return false;
    return config_off;
}

}  // namespace olduvai::presentation
