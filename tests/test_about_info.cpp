// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The About screen's text (spec 2026-09-18): what it says, that it fits the
// menu, and that it is plain ASCII — both text paths draw byte by byte.
#include "doctest/doctest.h"
#include "presentation/menu/about_info.hpp"
#include "presentation/menu/menu.hpp"

#include <string>
#include <vector>

using namespace olduvai::presentation;

namespace {

AboutBuild fake_build() {
    AboutBuild b;
    b.version = "0.9.7.1";
    b.build_id = "41642a1, 2026-09-18 10:41";
    b.os = "Linux";
    b.arch = "arm64";
    b.sdl = "SDL 2.30.9";
    b.features = {"MT-32", "MIDI out"};
    return b;
}

bool contains(const std::vector<std::string>& lines, const std::string& s) {
    for (const auto& l : lines)
        if (l.find(s) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST_CASE("about_lines: names the build, the author and the original game") {
    const auto lines = about_lines(fake_build(), kAboutChars);
    CHECK(lines.front() == "Olduvai 0.9.7.1");
    CHECK(contains(lines, "41642a1, 2026-09-18 10:41"));
    CHECK(contains(lines, "Linux arm64, SDL 2.30.9"));
    CHECK(contains(lines, "Features: MT-32, MIDI out"));
    CHECK(contains(lines, "Krzysztof Sokolowski"));
    CHECK(contains(lines, "github.com/ksokolowski/olduvai"));
    CHECK(contains(lines, "Prehistorik (1991, Titus)."));
    CHECK(contains(lines, "Requires your own game files."));
    CHECK(lines.size() == 10);   // a release build: 10 rows + Back
}

TEST_CASE("about_lines: fits the menu and is plain ASCII") {
    for (std::size_t w : {kAboutChars, std::size_t{24}}) {
        const auto lines = about_lines(fake_build(), w);
        CHECK(lines.size() <= kAboutRows);
        for (const auto& l : lines) {
            CHECK(l.size() <= w);
            CHECK(!l.empty());
            for (unsigned char c : l) CHECK((c >= 0x20 && c < 0x7F));
        }
    }
}

TEST_CASE("about_lines: no Features line when nothing optional is built in") {
    AboutBuild b = fake_build();
    b.features.clear();
    CHECK(!contains(about_lines(b, kAboutChars), "Features"));
}

TEST_CASE("about_build: the running binary's version and a known platform") {
    const AboutBuild b = about_build("SDL 2.0.0");
    CHECK(!b.version.empty());
    CHECK(!b.build_id.empty());
    CHECK(!b.os.empty());
    CHECK(b.sdl == "SDL 2.0.0");
}

TEST_CASE("fill_about_screen: readouts take the lines, the surplus goes") {
    MenuModel m;
    MenuScreen s;
    for (int i = 0; i < 4; ++i) {
        MenuItem it;
        it.id = "about" + std::to_string(i);
        it.type = "readout";
        s.items.push_back(it);
    }
    MenuItem back;
    back.id = "back";
    back.type = "back";
    back.label = "Back";
    s.items.push_back(back);
    m.screens["about"] = s;

    fill_about_screen(m, {"one", "two"});
    const auto& items = m.screens.at("about").items;
    REQUIRE(items.size() == 3);
    CHECK(items[0].label == "one");
    CHECK(items[1].label == "two");
    CHECK(items[2].type == "back");
}
