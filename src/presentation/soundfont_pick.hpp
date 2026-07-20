// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure SoundFont-selection precedence — SDL-free and filesystem-free so it can
// be unit-tested by injecting the `exists` predicate.  find_soundfont() in
// audio.cpp supplies the real directories, name list, and a real fs predicate.
//
// Precedence:
//   1. The user's config dir (~/.config/olduvai/soundfonts) is an absolute
//      override location: if it holds ANY recognised font, the highest-
//      preference name there wins.
//   2. System-wide, the most-preferred *font identity* wins regardless of which
//      system directory holds it — so a preferred face in one dir beats a
//      less-preferred face in another (Roland SC-55 in /usr/share/scummvm beats
//      FluidR3_GM in /usr/share/sounds/sf2).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace olduvai::presentation {

inline std::string select_soundfont(
    const std::string& config_dir,                 // "" when HOME is unset
    const std::vector<std::string>& system_dirs,   // searched in order
    const std::vector<std::string>& names,         // preference order (best first)
    const std::function<bool(const std::string&)>& exists) {
    const auto join = [](const std::string& d, const std::string& n) {
        return d + "/" + n;
    };
    // Phase 1: the user's config dir is an absolute override location.
    if (!config_dir.empty()) {
        for (const auto& n : names) {
            if (exists(join(config_dir, n))) return join(config_dir, n);
        }
    }
    // Phase 2: system-wide, the most-preferred font identity wins regardless of
    // which directory holds it (name is the outer loop, dir the inner).
    for (const auto& n : names) {
        for (const auto& d : system_dirs) {
            if (exists(join(d, n))) return join(d, n);
        }
    }
    return "";
}

}  // namespace olduvai::presentation
