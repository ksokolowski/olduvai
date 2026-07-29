// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Where to look for MT-32 / CM-32L ROM images, per platform.
//
// Split out of audio.cpp for exactly the reason `soundfont_pick.hpp` was: the
// SELECTION rule (which pair, which model) had tests while its INPUTS did not,
// and the whole bug was in the inputs.  The SoundFont half of that bug was
// fixed in c9c7a33; this is the other half, found 2026-07-28 while checking
// the backlog's "Windows discovery is POSIX-only" entry against source.
//
// The bug: the list was `$HOME/.config/olduvai/mt32-roms`, `$HOME/mt32-roms`
// and `./mt32-roms`.  Windows does not normally set `$HOME` — it sets
// `USERPROFILE` — so of those three only `./mt32-roms` resolved, and a Windows
// user had exactly one place to put ROMs: beside the executable.  That works
// for the portable zip, which is why it was not a visible failure, but there
// was no per-user location at all and nothing said so.
//
// A pure function can be pinned per platform by a test; a literal buried in
// audio.cpp could not be.  See tests/test_rom_dirs.cpp.
#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace olduvai::presentation {

// The directories to search for a ROM pair, most specific first.
//
// `rom_dir` is --rom-dir (empty when unset).  De-duplicated because --rom-dir
// and $OLDUVAI_MT32_ROMS frequently name the same place and this list is
// PRINTED in the "where I looked" diagnostic — a path shown twice reads like a
// bug in the search rather than a duplicate in the input.
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
    // No $HOME and no /usr/share: the per-user data dir, then (below) beside
    // the executable, which is how the portable zip is actually used.
    //
    // EXACTLY default_soundfont_dirs()'s Windows arm, one directory renamed.
    // That is the whole design: both backends need a user-supplied asset the
    // user has to go and place, so there is one convention to learn, not two.
    // Resist adding %USERPROFILE% or a second spelling here unless the
    // SoundFont side grows it too — divergence between them is the bug this
    // file was written to end, not a feature.
    if (const char* appdata = std::getenv("LOCALAPPDATA")) {
        add(std::string(appdata) + "\\olduvai\\mt32-roms");
    }
#endif

    // POSIX locations.  Left on every platform, like the Linux SoundFont
    // paths: harmless when $HOME is unset, and a Windows user running under
    // MSYS/Cygwin or a shell that does set $HOME gets them for free.
    if (const char* home = std::getenv("HOME")) {
        add(std::string(home) + "/.config/olduvai/mt32-roms");
        add(std::string(home) + "/mt32-roms");
    }
    add("./mt32-roms");
    return dirs;
}

}  // namespace olduvai::presentation
