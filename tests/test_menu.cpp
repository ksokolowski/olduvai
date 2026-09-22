// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Menu navigation core — C++ mirror of the reference implementation's
// menu-controller tests.  Builds an in-memory MenuModel (the real
// data/menus.json loader is Phase-3 wiring) and exercises the state machine.

#include "doctest/doctest.h"
#include "presentation/menu/menu.hpp"

#include <map>
#include <string>

using namespace olduvai::presentation;

namespace {

struct FakeBindings : MenuBindings {
    std::map<std::string, std::string> v;
    std::string get(const std::string& k) override {
        auto it = v.find(k);
        return it == v.end() ? std::string{} : it->second;
    }
    void set(const std::string& k, const std::string& val) override { v[k] = val; }
};

MenuModel make_model() {
    MenuModel m;
    MenuScreen main;
    main.header = "OLDUVAI";
    main.items = {
        MenuItem{"start", "action", "Start Game", "", "", "start_game", "", {}, 0, 0, 0, false, {}},
        MenuItem{"options", "submenu", "Options", "", "options", "", "", {}, 0, 0, 0, false, {}},
        MenuItem{"quit", "action", "Quit", "", "", "quit_desktop", "", {}, 0, 0, 0, false, {}},
    };
    MenuScreen opts;
    opts.header = "OPTIONS";
    MenuItem tog{"tog", "toggle", "Toggle", "", "", "", "t", {}, 0, 0, 0, false, {}};
    MenuItem ch{"ch", "choice", "Choice", "", "", "", "c", {"a", "b", "x"}, 0, 0, 0, false, {}};
    MenuItem sld{"sld", "slider", "Vol", "", "", "", "v", {}, 0, 100, 5, false, {}};
    MenuItem ro{"ro", "readout", "readout-line", "", "", "", "", {}, 0, 0, 0, false, {}};
    MenuItem bk{"back", "back", "Back", "", "", "", "", {}, 0, 0, 0, false, {}};
    opts.items = {tog, ch, sld, ro, bk};
    m.screens["main"] = main;
    m.screens["options"] = opts;
    return m;
}

}  // namespace

TEST_CASE("open starts at first selectable + header") {
    auto model = make_model();
    FakeBindings b;
    Menu menu(model, b);
    menu.open("main");
    CHECK(menu.is_open());
    CHECK(menu.current_screen() == "main");
    CHECK(menu.cursor_index() == 0);
    CHECK(menu.header() == "OLDUVAI");
}

TEST_CASE("move wraps") {
    auto model = make_model();
    FakeBindings b;
    Menu menu(model, b);
    menu.open("main");  // 3 selectable items
    menu.move(-1);
    CHECK(menu.cursor_index() == 2);
    menu.move(+1);
    CHECK(menu.cursor_index() == 0);
}

TEST_CASE("submenu push + back pop; back at root closes") {
    auto model = make_model();
    FakeBindings b;
    Menu menu(model, b);
    menu.open("main");
    menu.move(+1);  // Options
    menu.activate();
    CHECK(menu.current_screen() == "options");
    menu.back();
    CHECK(menu.current_screen() == "main");
    menu.back();
    CHECK_FALSE(menu.is_open());
}

TEST_CASE("toggle flips via adjust and Enter") {
    auto model = make_model();
    FakeBindings b;
    b.set("t", "0");
    Menu menu(model, b);
    menu.open("options");  // first selectable = Toggle
    CHECK(menu.rows()[menu.cursor_index()].label == "Toggle");
    menu.adjust(+1);
    CHECK(b.get("t") == "1");
    menu.activate();
    CHECK(b.get("t") == "0");
}

TEST_CASE("choice cycles and wraps") {
    auto model = make_model();
    FakeBindings b;
    b.set("c", "a");
    Menu menu(model, b);
    menu.open("options");
    menu.move(+1);  // Choice
    menu.adjust(+1);
    CHECK(b.get("c") == "b");
    b.set("c", "a");
    menu.adjust(-1);
    CHECK(b.get("c") == "x");  // wrap backwards
}

TEST_CASE("choice: an unlisted value steps into the list, it does not skip") {
    // REGRESSION (2026-09-07).  adjust() used to read
    //   int i = pos == vals.end() ? 0 : ...
    // so a current token absent from the row's list was treated as index 0 and
    // ONE keypress landed on values[1] — skipping values[0] entirely and
    // giving no hint that the held value had been discarded.  Live case:
    // Aspect held "widescreen" while its authored list was keep/4:3/stretch,
    // so opening Video and nudging the row jumped straight to "4:3".
    //
    // What this guarantees is movement by one step from where you actually
    // are.  It does NOT make the old value permanently reachable — that is
    // the job of listing it (menus.json) and of never withholding a value the
    // user holds (StagingBindings::allowed_values).
    auto model = make_model();
    FakeBindings b;
    Menu menu(model, b);
    menu.open("options");
    menu.move(+1);   // Choice ("a","b","x")

    b.set("c", "unlisted");
    menu.adjust(+1);
    CHECK(b.get("c") == "a");   // was "b" before the fix

    b.set("c", "unlisted");
    menu.adjust(-1);
    CHECK(b.get("c") == "x");   // and symmetric the other way
}

TEST_CASE("choice: allowed_values narrows what the row cycles through") {
    // The Aspect gate withholds "widescreen" from the cycle in classic mode,
    // where selecting it would do nothing (the presenter needs hd()).
    struct Narrowing : FakeBindings {
        std::vector<std::string> allowed_values(const MenuItem& it) override {
            std::vector<std::string> out;
            for (const auto& v : it.values)
                if (v != "b") out.push_back(v);   // "b" is inert right now
            return out;
        }
    };
    auto model = make_model();
    Narrowing b;
    b.set("c", "a");
    Menu menu(model, b);
    menu.open("options");
    menu.move(+1);

    menu.adjust(+1);
    CHECK(b.get("c") == "x");   // "b" skipped, never offered
    menu.adjust(+1);
    CHECK(b.get("c") == "a");   // wraps within the narrowed list
}

TEST_CASE("slider clamps at bounds") {
    auto model = make_model();
    FakeBindings b;
    b.set("v", "0");
    Menu menu(model, b);
    menu.open("options");
    menu.move(+1);  // Choice
    menu.move(+1);  // Vol slider
    CHECK(menu.rows()[menu.cursor_index()].label == "Vol");
    menu.adjust(-1);
    CHECK(b.get("v") == "0");  // clamp at min
    for (int i = 0; i < 40; ++i) menu.adjust(+1);
    CHECK(b.get("v") == "100");  // clamp at max
}

TEST_CASE("action dispatches by id and invokes callback") {
    auto model = make_model();
    FakeBindings b;
    int calls = 0;
    MenuActionTable actions{{"start_game", [&] { ++calls; }}};
    Menu menu(model, b, actions);
    menu.open("main");
    const std::string aid = menu.activate();
    CHECK(aid == "start_game");
    CHECK(calls == 1);
}

TEST_CASE("readout is non-selectable and skipped by the cursor") {
    auto model = make_model();
    FakeBindings b;
    Menu menu(model, b);
    menu.open("options");
    bool readout_selected = false;
    for (int i = 0; i < 12; ++i) {
        if (menu.rows()[menu.cursor_index()].label == "readout-line") readout_selected = true;
        menu.move(+1);
    }
    CHECK_FALSE(readout_selected);
    const auto rows = menu.rows();
    CHECK_FALSE(rows[3].selectable);  // the readout row
}

TEST_CASE("dangling submenu target is ignored (stale menus.json)") {
    // A submenu whose target screen is missing (stale build copy or a
    // hand-edited menus.json) must be a no-op — pushing it made the render
    // path walk an empty/unknown screen and crash.
    auto model = make_model();
    model.screens["main"].items[1].target = "no_such_screen";
    FakeBindings b;
    Menu menu(model, b);
    menu.open("main");
    menu.move(+1);  // Options → dangling target
    menu.activate();
    CHECK(menu.is_open());
    CHECK(menu.current_screen() == "main");   // still on the same screen
    // Unknown top-level id: open() refuses and the menu reports closed.
    Menu menu2(model, b);
    menu2.open("no_such_screen");
    CHECK_FALSE(menu2.is_open());
}

TEST_CASE("empty-items screen refuses to open or be pushed") {
    // menus.json screens are schema-required to have items, but the file is
    // user-editable: an empty screen must be inert, not a first-keypress
    // crash in selected() (review 2026-07-03 S5).
    auto model = make_model();
    MenuScreen empty;
    empty.header = "EMPTY";
    model.screens["empty"] = empty;
    model.screens["main"].items[1].target = "empty";
    FakeBindings b;
    Menu menu(model, b);
    menu.open("empty");
    CHECK_FALSE(menu.is_open());
    menu.open("main");
    menu.move(+1);          // Options → retargeted at the empty screen
    menu.activate();
    CHECK(menu.current_screen() == "main");   // push ignored
}

TEST_CASE("action row with values cycles like a choice and still activates") {
    // The main menu's Start Game level select: Enter fires the action,
    // left/right cycles the bound value (mirrors data/menus.json "start").
    auto model = make_model();
    auto& start = model.screens["main"].items[0];
    start.key = "menu.start_level";
    start.values = {"1", "2", "3"};
    start.value_labels = {"Level 1", "Level 2", "Level 3"};
    FakeBindings b;
    b.v["menu.start_level"] = "1";
    Menu m(model, b);
    m.open("main");
    CHECK(m.rows()[0].value == std::optional<std::string>("Level 1"));
    m.adjust(+1);
    CHECK(b.v["menu.start_level"] == "2");
    m.adjust(-1);
    m.adjust(-1);   // 2 -> 1 -> wraps to 3
    CHECK(b.v["menu.start_level"] == "3");
    CHECK(m.rows()[0].value == std::optional<std::string>("Level 3"));
    CHECK(m.activate() == "start_game");   // Enter still fires the action
    // A plain action row (no values) is untouched by left/right.
    m.move(+1); m.move(+1);                // start -> options -> quit
    m.adjust(+1);
    CHECK(m.activate() == "quit_desktop");
}

TEST_CASE("text row previews first line and activate returns an edit sentinel") {
    auto model = make_model();
    auto& r = model.screens["main"].items[0];
    r.type = "text";
    r.key = "report.description";
    FakeBindings b;
    b.v["report.description"] = "first line\nsecond line";
    Menu m(model, b);
    m.open("main");
    CHECK(m.rows()[0].value == std::optional<std::string>("first line"));
    CHECK(m.activate() == "__edit_text:report.description");
}

TEST_CASE("menu: a CLOSED menu answers, it does not read off the end") {
    // REGRESSION (TrimUI, 2026-09-20).  current_screen() returned
    // stack_.back().first with no emptiness check, and screen() fed that to
    // the tree's only map::at.  Asking a closed menu what it shows was
    // undefined behaviour whose symptom — on the device, not on the dev Mac —
    // was an out_of_range escaping to run_game's handler, which tells the
    // player their game files are corrupt.
    auto model = make_model();
    FakeBindings b;
    Menu menu(model, b);
    CHECK(!menu.is_open());
    CHECK(menu.current_screen().empty());
    CHECK(menu.cursor_index() == 0);
    CHECK(menu.rows().empty());
    CHECK(menu.header().empty());
    menu.move(+1);          // and none of the navigation throws either
    menu.adjust(+1);
    menu.back();
    CHECK(menu.activate().empty());
    CHECK(!menu.is_open());
}

TEST_CASE("menu: an unknown screen renders as nothing, not as a throw") {
    auto model = make_model();
    FakeBindings b;
    Menu menu(model, b);
    menu.open("no-such-screen");
    CHECK(!menu.is_open());     // open() refuses it
    CHECK(menu.rows().empty());
    CHECK(menu.header().empty());
}
