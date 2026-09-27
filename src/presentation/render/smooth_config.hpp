// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Smooth-motion present tuning as config (smooth_subframes, smooth_vsync),
// resolved env var > config key > refresh-derived default.  The env vars
// (OLDUVAI_SMOOTH_SUBFRAMES, OLDUVAI_NO_VSYNC) remain debug overrides.
// SDL-free, so options_build.cpp (unit tier) can refresh it;
// tests/test_smooth_config.cpp pins the rule.

#pragma once

#include <cstdlib>
#include <string>

#include "presentation/env_num.hpp"   // parse_int

namespace olduvai::presentation {

struct SmoothPresentConfig {
    int subframes = 0;        // 0 = auto (refresh-derived); 1..12 = explicit
    bool vsync_off = false;   // smooth_vsync=off: force the discrete path
};

// The process-wide value the pacers read at each frame-loop start.  run_game
// publishes it; the Options persist hook refreshes it, so an Enhanced switch
// paces with the new values at once.
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

// Sub-frame count.  Env set: [1,12] wins, anything else keeps `derived`.
// Unset: a config value in [1,12] wins; 0 means `derived`.
inline int resolve_subframe_count(const char* env, int config, int derived) {
    if (env != nullptr) {
        // atoi is safe: garbage parses to 0, outside [1,12], so it keeps the
        // default.
        // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
        const int v = std::atoi(env);
        return (v >= 1 && v <= 12) ? v : derived;
    }
    return (config >= 1 && config <= 12) ? config : derived;
}

// Was the count asked for (a ceiling on the vsync fill) or derived (a hint)?
// Only an asked-for count caps (see smooth_present.hpp).  A set env var counts
// even when invalid.
inline bool resolve_subframes_explicit(const char* env, int config) {
    return env != nullptr || (config >= 1 && config <= 12);
}

// Force the discrete fallback?  Any OLDUVAI_NO_VSYNC value does.  The config
// knob is ignored under SDL's kmsdrm driver, where vsync off means async page
// flips and a driver without them rejects every present ("Could not queue
// pageflip: -22"; Powkiddy A12: 1747 errors in 260 ticks).  The TrimUI's mali
// driver keeps it.  `video_driver`: SDL_GetCurrentVideoDriver(), null before
// video is up.
inline bool resolve_vsync_off(const char* env, bool config_off,
                              const char* video_driver = nullptr) {
    if (env != nullptr) return true;
    if (config_off && video_driver != nullptr &&
        std::string(video_driver) == "kmsdrm")
        return false;
    return config_off;
}

}  // namespace olduvai::presentation
