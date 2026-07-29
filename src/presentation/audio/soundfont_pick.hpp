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

#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace olduvai::presentation {

// The system directories to search, per platform.  This lives beside the
// selection rule — and is a FUNCTION rather than a literal at the call site —
// because the rule was unit-tested while its INPUTS were not, and the bug was
// entirely in the inputs: the list held the three Linux paths only, so on
// macOS a user who followed our own advice (`brew install scummvm`) had
// Roland_SC-55.sf2 at /opt/homebrew/share/scummvm and the engine looked in
// /usr/share/scummvm, which does not exist there.  GM stayed silent with no
// explanation.  A pure function can be pinned per platform by a test; a
// literal buried in audio.cpp could not be.
inline std::vector<std::string> default_soundfont_dirs() {
    std::vector<std::string> dirs;
#ifdef __APPLE__
    // Homebrew arm64 prefix, then the Intel prefix, then the OS bank dirs.
    dirs = {"/opt/homebrew/share/scummvm",
            "/opt/homebrew/share/sounds/sf2",
            "/opt/homebrew/share/soundfonts",
            "/usr/local/share/scummvm",
            "/usr/local/share/sounds/sf2",
            "/usr/local/share/soundfonts",
            "/Library/Audio/Sounds/Banks"};
    if (const char* home = std::getenv("HOME")) {
        dirs.emplace_back(std::string(home) + "/Library/Audio/Sounds/Banks");
    }
#elif defined(_WIN32)
    // No /usr/share at all: look beside the executable (how the portable zip
    // is used) and in the per-user data dir.
    dirs = {"soundfonts", "./soundfonts"};
    if (const char* appdata = std::getenv("LOCALAPPDATA")) {
        dirs.emplace_back(std::string(appdata) + "\\olduvai\\soundfonts");
    }
#endif
    // Linux/BSD locations stay on every platform: harmless when absent, and
    // they are what a distro package or a manual install actually uses.
    dirs.emplace_back("/usr/share/sounds/sf2");
    dirs.emplace_back("/usr/share/soundfonts");
    dirs.emplace_back("/usr/share/scummvm");
    return dirs;
}

}  // namespace olduvai::presentation

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
