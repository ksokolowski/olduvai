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
        "pause", "pause_boss", "dev", "bug_report"};
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
