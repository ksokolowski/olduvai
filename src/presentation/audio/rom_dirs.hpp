// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Where to look for MT-32 / CM-32L ROM images, per platform; a pure function
// so tests/test_rom_dirs.cpp can pin each platform's list.  Windows rarely sets
// $HOME, so a POSIX-only list leaves Windows users just the folder beside the
// executable, silently.
#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace olduvai::presentation {

// Directories to search for a ROM pair, most specific first.  `rom_dir` is
// --rom-dir (empty when unset).  De-duplicated: the list is printed in the
// "where I looked" diagnostic.
inline std::vector<std::string> rom_search_dirs(const std::string& rom_dir) {
    std::vector<std::string> dirs;
    const auto add = [&dirs](std::string d) {
        if (!d.empty() &&
            std::find(dirs.begin(), dirs.end(), d) == dirs.end()) {
            dirs.push_back(std::move(d));
        }
    };
    add(rom_dir);
    if (const char* env = std::getenv("OLDUVAI_MT32_ROMS")) add(env);

#ifdef _WIN32
    // Windows: the per-user data dir, then (below) beside the executable (the
    // portable zip).  Mirrors default_soundfont_dirs()'s Windows arm on
    // purpose: one convention for both user-supplied assets.
    if (const char* appdata = std::getenv("LOCALAPPDATA")) {
        add(std::string(appdata) + "\\olduvai\\mt32-roms");
    }
#endif

    // POSIX locations on every platform: harmless without $HOME, and useful
    // under MSYS/Cygwin.
    if (const char* home = std::getenv("HOME")) {
        add(std::string(home) + "/.config/olduvai/mt32-roms");
        add(std::string(home) + "/mt32-roms");
    }
    add("./mt32-roms");
    return dirs;
}

}  // namespace olduvai::presentation
