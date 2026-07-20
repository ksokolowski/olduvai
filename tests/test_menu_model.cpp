// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// menus.json loader — nested-JSON parser + MenuModel mapping, and a parity
// check that olduvai parses the SAME data/menus.json the Python tests validate
// (completes the cross-engine id-parity plan, Task 2.3).

#include "doctest/doctest.h"
#include "presentation/menu_model.hpp"

#include <set>
#include <string>

using namespace olduvai::presentation;

#ifndef OLDUVAI_SOURCE_DIR
#define OLDUVAI_SOURCE_DIR "."
#endif

TEST_CASE("parse_menus_json: nested objects/arrays/types map to MenuModel") {
    const std::string text = R"({
      "version": 1,
      "screens": {
        "main": {
          "header": "OLDUVAI",
          "items": [
            { "id": "start", "type": "action", "label": "Start", "action": "start_game" },
            { "id": "opts", "type": "submenu", "label": "Options", "target": "options", "hint": "4" }
          ]
        },
        "options": {
          "header": "OPTIONS",
          "items": [
            { "id": "dev", "type": "choice", "label": "Device", "key": "d", "values": ["mt32", "off"] },
            { "id": "scale", "type": "choice", "label": "Scale", "key": "s", "values": [1, 2, 4], "restart": true },
            { "id": "vol", "type": "slider", "label": "Vol", "key": "v", "min": 0, "max": 100, "step": 5 },
            { "id": "back", "type": "back", "label": "Back" }
          ]
        }
      }
    })";

    const MenuModel m = parse_menus_json(text);
    CHECK(m.screens.size() == 2);
    CHECK(m.screens.at("main").header == "OLDUVAI");

    const auto& items = m.screens.at("main").items;
    CHECK(items[0].type == "action");
    CHECK(items[0].action == "start_game");
    CHECK(items[1].type == "submenu");
    CHECK(items[1].target == "options");
    CHECK(items[1].hint == "4");

    const auto& opt = m.screens.at("options").items;
    CHECK(opt[0].values == std::vector<std::string>{"mt32", "off"});
    CHECK(opt[1].values == std::vector<std::string>{"1", "2", "4"});  // numbers → tokens
    CHECK(opt[1].restart == true);
    CHECK(opt[2].type == "slider");
    CHECK(opt[2].min == 0.0);
    CHECK(opt[2].max == 100.0);
    CHECK(opt[2].step == 5.0);
}

TEST_CASE("parse_menus_json: malformed input throws") {
    CHECK_THROWS(parse_menus_json("{ \"screens\": "));
    CHECK_THROWS(parse_menus_json("not json"));
}

TEST_CASE("load_menus: olduvai parses the canonical data/menus.json (parity)") {
    auto model = load_menus(std::string(OLDUVAI_SOURCE_DIR) + "/assets/data/menus.json");
    REQUIRE(model.has_value());

    std::set<std::string> ids;
    for (const auto& [sid, _] : model->screens) ids.insert(sid);

    const std::set<std::string> expected = {
        "main", "options", "audio", "video", "enhancements",
        "cave_paintings", "cheats", "cheat_bonus", "pause",
        "pause_boss", "dev", "bug_report"};
    CHECK(ids == expected);
    CHECK(model->screens.at("main").header == "OLDUVAI");
    // Spot-check a known binding survived the round-trip.
    bool found_god = false;
    for (const auto& it : model->screens.at("cheats").items)
        if (it.id == "god") { CHECK(it.type == "toggle"); CHECK(it.key == "cheat.god"); found_god = true; }
    CHECK(found_god);
}

TEST_CASE("parse_menus_json: pathological inputs fail cleanly") {
    // Depth cap — ~100k '[' must throw the parser's own error, not blow the
    // stack (no catch can survive stack exhaustion).
    CHECK_THROWS(parse_menus_json(std::string(100000, '[')));
    // Out-of-range number literal → parser error, not a stray out_of_range.
    CHECK_THROWS(parse_menus_json("{\"a\": 1e999999}"));
}
