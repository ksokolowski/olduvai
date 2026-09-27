// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SoundFont selection precedence, pure (the `exists` predicate is injected for
// tests; find_soundfont() in audio.cpp supplies the real ones):
//   1. ~/.config/olduvai/soundfonts overrides: if it holds any recognised
//      font, the most-preferred name there wins.
//   2. Otherwise the most-preferred font wins whichever system dir holds it
//      (SC-55 in /usr/share/scummvm beats FluidR3_GM in /usr/share/sounds/sf2).
#pragma once

#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace olduvai::presentation {

// The system directories to search, per platform; a function so a test pins
// each platform's list (Homebrew installs to /opt/homebrew/share, not
// /usr/share).
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
