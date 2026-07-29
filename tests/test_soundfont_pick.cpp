// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Precedence tests for select_soundfont() — the pure SoundFont-selection core.
// Filesystem is mocked via an injected `exists` set so the rules are pinned
// without touching real directories.
#include "doctest/doctest.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "presentation/audio/soundfont_pick.hpp"

using olduvai::presentation::select_soundfont;

namespace {
// Preference order the engine uses: Roland SC-55 first (matches the Windows
// Sound Canvas / gm.dls character), then the clean-provenance faces.
const std::vector<std::string> kNames = {
    "Roland_SC-55.sf2", "GeneralUser-GS.sf2", "GeneralUser GS.sf2",
    "FluidR3_GM.sf2", "default-GM.sf2"};
const std::vector<std::string> kSysDirs = {
    "/usr/share/sounds/sf2", "/usr/share/soundfonts", "/usr/share/scummvm"};

auto present(std::set<std::string> have) {
    return [have = std::move(have)](const std::string& p) {
        return have.count(p) != 0;
    };
}
}  // namespace

TEST_CASE("system-wide: most-preferred font identity wins across directories") {
    // FluidR3 sits in the first dir; SC-55 in the last.  SC-55 must still win.
    auto exists = present({"/usr/share/sounds/sf2/FluidR3_GM.sf2",
                           "/usr/share/scummvm/Roland_SC-55.sf2"});
    CHECK(select_soundfont("", kSysDirs, kNames, exists) ==
          "/usr/share/scummvm/Roland_SC-55.sf2");
}

TEST_CASE("system-wide: falls through to a lesser face when SC-55 is absent") {
    auto exists = present({"/usr/share/sounds/sf2/FluidR3_GM.sf2"});
    CHECK(select_soundfont("", kSysDirs, kNames, exists) ==
          "/usr/share/sounds/sf2/FluidR3_GM.sf2");
}

TEST_CASE("config dir is an absolute override — beats a better system font") {
    // User dropped FluidR3 in their config dir; SC-55 exists system-wide.
    // The config dir wins even though SC-55 is the higher preference.
    auto exists = present({"/home/u/.config/olduvai/soundfonts/FluidR3_GM.sf2",
                           "/usr/share/scummvm/Roland_SC-55.sf2"});
    CHECK(select_soundfont("/home/u/.config/olduvai/soundfonts", kSysDirs,
                           kNames, exists) ==
          "/home/u/.config/olduvai/soundfonts/FluidR3_GM.sf2");
}

TEST_CASE("config dir honours name preference within itself") {
    auto exists = present({"/cfg/FluidR3_GM.sf2", "/cfg/Roland_SC-55.sf2"});
    CHECK(select_soundfont("/cfg", kSysDirs, kNames, exists) ==
          "/cfg/Roland_SC-55.sf2");
}

TEST_CASE("no font anywhere yields an empty string") {
    CHECK(select_soundfont("/cfg", kSysDirs, kNames, present({})).empty());
}

TEST_CASE("empty config dir skips phase 1") {
    auto exists = present({"/usr/share/scummvm/Roland_SC-55.sf2"});
    CHECK(select_soundfont("", kSysDirs, kNames, exists) ==
          "/usr/share/scummvm/Roland_SC-55.sf2");
}

// The list of directories to search — pinned per platform.
//
// WHY THIS TEST EXISTS.  Every case above tests the selection RULE, and the
// rule was always correct.  The bug was in the rule's INPUT: the list held
// three Linux paths only, so on macOS a user who followed our own README and
// ran `brew install scummvm` had Roland_SC-55.sf2 sitting at
// /opt/homebrew/share/scummvm while the engine looked in /usr/share/scummvm.
// GM stayed silent and said nothing about why.  A well-tested pure function
// with an untested argument is still a broken feature.
TEST_CASE("default_soundfont_dirs covers this platform's real locations") {
    const auto dirs = olduvai::presentation::default_soundfont_dirs();
    const auto has = [&dirs](const std::string& d) {
        return std::find(dirs.begin(), dirs.end(), d) != dirs.end();
    };

    // The Linux locations are searched everywhere: harmless when absent, and
    // they are what a distro package actually installs to.
    CHECK(has("/usr/share/sounds/sf2"));
    CHECK(has("/usr/share/soundfonts"));
    CHECK(has("/usr/share/scummvm"));

#ifdef __APPLE__
    // Measured: `brew install scummvm` puts the font here.
    CHECK(has("/opt/homebrew/share/scummvm"));
    CHECK(has("/usr/local/share/scummvm"));       // Intel prefix
    CHECK(has("/Library/Audio/Sounds/Banks"));    // the OS's own bank dir
    // Homebrew must be searched BEFORE /usr/share, which does not exist here.
    const auto brew = std::find(dirs.begin(), dirs.end(),
                                std::string("/opt/homebrew/share/scummvm"));
    const auto usr = std::find(dirs.begin(), dirs.end(),
                               std::string("/usr/share/scummvm"));
    CHECK(brew < usr);
#endif
#ifdef _WIN32
    // Windows has no /usr/share at all; the portable zip is used in place.
    CHECK(has("soundfonts"));
    CHECK(has("./soundfonts"));
#endif
}
