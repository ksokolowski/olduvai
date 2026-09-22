// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The menu model is GENERATED from assets/data/menus.json at build time by
// cmake/gen_menu_model.cmake, so the questions worth asking here changed.
//
// Gone: the parser tests (malformed JSON, unterminated input, a 100k-deep '['
// nest, 1e999999).  There is no runtime parser left to harden — a malformed
// menus.json now fails the build, and none of those inputs can reach a user.
//
// Kept: that the SHIPPED menus.json produces the model the engine expects.
// That is about the content, not the transport, and it survives the change
// intact — including the screen set, which is what caught the stale entries
// when the enhanced-mode toggles were removed.

#include "doctest/doctest.h"
#include "presentation/menu/menu_model.hpp"

#include <set>
#include <string>
#include <vector>

using namespace olduvai::presentation;

TEST_CASE("built_in_menu_model: the shipped menus.json yields the expected screens") {
    const MenuModel m = built_in_menu_model();

    std::set<std::string> ids;
    for (const auto& [sid, _] : m.screens) ids.insert(sid);

    const std::set<std::string> expected = {
        "main", "options", "audio", "video", "cheats", "cheat_bonus",
        "pause", "pause_boss", "dev", "bug_report", "quit", "about",
        "audio_advanced"};
    CHECK(ids == expected);
    CHECK(m.screens.at("main").header == "OLDUVAI");
}

TEST_CASE("built_in_menu_model: bindings survive generation") {
    const MenuModel m = built_in_menu_model();

    // A toggle with its config key.
    bool found_god = false;
    for (const auto& it : m.screens.at("cheats").items) {
        if (it.id == "god") {
            CHECK(it.type == "toggle");
            CHECK(it.key == "cheat.god");
            found_god = true;
        }
    }
    CHECK(found_god);

    // A choice whose JSON values are NUMBERS.  They must arrive as the same
    // integer tokens the old parser produced through num_token(), because the
    // config layer compares them as strings — this is the one place the
    // generator had to reproduce a conversion rather than copy text.
    bool found_scale = false;
    for (const auto& it : m.screens.at("video").items) {
        if (it.id == "render_scale") {
            CHECK(it.values == std::vector<std::string>{"2", "4"});
            CHECK(it.restart);
            found_scale = true;
        }
    }
    CHECK(found_scale);

    // A slider carries its numeric bounds.
    bool found_vol = false;
    for (const auto& it : m.screens.at("audio").items) {
        if (it.id == "music_volume") {
            CHECK(it.min == doctest::Approx(0.0));
            CHECK(it.max == doctest::Approx(100.0));
            CHECK(it.step == doctest::Approx(5.0));
            found_vol = true;
        }
    }
    CHECK(found_vol);
}

TEST_CASE("built_in_menu_model: one Quit entry, and About on the title menu") {
    const MenuModel m = built_in_menu_model();

    // Both pause menus: a single Quit submenu, no top-level quit actions.
    for (const char* sid : {"pause", "pause_boss"}) {
        int quit_submenus = 0;
        for (const auto& it : m.screens.at(sid).items) {
            CHECK(it.action != "quit_title");
            CHECK(it.action != "quit_desktop");
            if (it.id == "quit") {
                CHECK(it.type == "submenu");
                CHECK(it.target == "quit");
                CHECK(it.label == "Quit");
                ++quit_submenus;
            }
        }
        CHECK(quit_submenus == 1);
    }

    // The Quit screen: to title, exit, back — the action ids the three
    // drivers already bind.
    const auto& q = m.screens.at("quit").items;
    REQUIRE(q.size() == 3);
    CHECK(q[0].action == "quit_title");
    CHECK(q[1].action == "quit_desktop");
    CHECK(q[1].label == "Exit Game");
    CHECK(q[2].type == "back");

    // Title menu ends Options, About, Quit.
    const auto& mi = m.screens.at("main").items;
    REQUIRE(mi.size() >= 3);
    CHECK(mi[mi.size() - 3].id == "options");
    CHECK(mi[mi.size() - 2].id == "about");
    CHECK(mi[mi.size() - 2].target == "about");
    CHECK(mi.back().id == "quit");
    CHECK(mi.back().label == "Quit");
    CHECK(mi.back().action == "quit_desktop");

    // About: readouts (filled at runtime), then Back.
    const auto& a = m.screens.at("about").items;
    REQUIRE(a.size() >= 2);
    for (std::size_t i = 0; i + 1 < a.size(); ++i)
        CHECK(a[i].type == "readout");
    CHECK(a.back().type == "back");
}
