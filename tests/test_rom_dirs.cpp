// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pins the MT-32 / CM-32L ROM search list per platform.
//
// This test is the point of the header existing.  The SoundFont search had the
// same defect and the same shape: the SELECTION rule was tested, the DIRECTORY
// LIST was not, and the bug lived entirely in the list.  Fixing the SoundFont
// half without pinning the ROM half is how the ROM half survived four
// releases.
#include "doctest/doctest.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "presentation/audio/rom_dirs.hpp"

using olduvai::presentation::rom_search_dirs;

namespace {

bool has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// index of `s`, or -1 — for ordering assertions.
int idx(const std::vector<std::string>& v, const std::string& s) {
    const auto it = std::find(v.begin(), v.end(), s);
    return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

}  // namespace

TEST_CASE("rom_search_dirs puts --rom-dir first") {
    const auto dirs = rom_search_dirs("/somewhere/roms");
    REQUIRE(!dirs.empty());
    CHECK(dirs.front() == "/somewhere/roms");
}

TEST_CASE("rom_search_dirs de-duplicates --rom-dir against the environment") {
    // The diagnostic PRINTS this list, so a repeat reads as a search bug.
    const char* prev = std::getenv("OLDUVAI_MT32_ROMS");
    const std::string saved = prev != nullptr ? prev : "";
#ifndef _WIN32
    setenv("OLDUVAI_MT32_ROMS", "/dup/roms", 1);
#else
    _putenv_s("OLDUVAI_MT32_ROMS", "/dup/roms");
#endif
    const auto dirs = rom_search_dirs("/dup/roms");
    CHECK(std::count(dirs.begin(), dirs.end(), std::string("/dup/roms")) == 1);
#ifndef _WIN32
    if (saved.empty()) unsetenv("OLDUVAI_MT32_ROMS");
    else setenv("OLDUVAI_MT32_ROMS", saved.c_str(), 1);
#else
    _putenv_s("OLDUVAI_MT32_ROMS", saved.c_str());
#endif
}

TEST_CASE("rom_search_dirs always offers the beside-the-executable location") {
    // The one path that works on every platform with no environment at all,
    // and the only one that worked on Windows before 2026-07-28.
    CHECK(has(rom_search_dirs(""), "./mt32-roms"));
}

TEST_CASE("rom_search_dirs covers this platform's real locations") {
    const auto dirs = rom_search_dirs("");

#ifdef _WIN32
    // The regression this file exists for: Windows does not set $HOME, so
    // before the fix the ONLY entry here was "./mt32-roms".
    if (const char* appdata = std::getenv("LOCALAPPDATA")) {
        CHECK(has(dirs, std::string(appdata) + "\\olduvai\\mt32-roms"));
        // A per-user location must be searched before the exe's own
        // directory: a zip unpacked into Program Files is not writable by the
        // user, and unzipping a new version over the old one would eat ROMs
        // stored beside the exe.
        CHECK(idx(dirs, std::string(appdata) + "\\olduvai\\mt32-roms") <
              idx(dirs, "./mt32-roms"));
    }
#else
    // POSIX: the config dir is the documented home, ~/mt32-roms the
    // convenience alias, and the config dir must win.
    if (const char* home = std::getenv("HOME")) {
        const std::string cfg = std::string(home) + "/.config/olduvai/mt32-roms";
        const std::string alias = std::string(home) + "/mt32-roms";
        CHECK(has(dirs, cfg));
        CHECK(has(dirs, alias));
        CHECK(idx(dirs, cfg) < idx(dirs, alias));
        CHECK(idx(dirs, alias) < idx(dirs, "./mt32-roms"));
    }
#endif
}
