// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Precedence tests for select_soundfont() — the pure SoundFont-selection core.
// Filesystem is mocked via an injected `exists` set so the rules are pinned
// without touching real directories.
#include "doctest/doctest.h"

#include <set>
#include <string>
#include <vector>

#include "presentation/soundfont_pick.hpp"

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
