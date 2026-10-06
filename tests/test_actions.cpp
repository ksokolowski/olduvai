// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The action table: every default key name resolves, the keys a screen used
// to list by hand mean the same action everywhere (input/actions.hpp), and a
// pad press sends what its context says (gamepad::key_for_input).

#include "doctest/doctest.h"

#include <SDL.h>

#include <vector>

#include "presentation/input/actions.hpp"
#include "presentation/input/gamepad.hpp"
#include "presentation/menu/cheat_picker.hpp"

using olduvai::presentation::Action;
using olduvai::presentation::CheatPicker;
using olduvai::presentation::default_keys;
using olduvai::presentation::kActionCount;
using olduvai::presentation::key_is;

TEST_CASE("actions: every default key name resolves") {
    for (std::size_t i = 0; i < kActionCount; ++i)
        for (const std::string& name : default_keys(static_cast<Action>(i))) {
            CAPTURE(name);
            CHECK(SDL_GetKeyFromName(name.c_str()) != SDLK_UNKNOWN);
            CHECK(SDL_GetScancodeFromName(name.c_str()) !=
                  SDL_SCANCODE_UNKNOWN);
        }
}

TEST_CASE("actions: confirm, back and the movement keys") {
    CHECK(key_is(SDLK_RETURN, Action::kConfirm));
    CHECK(key_is(SDLK_KP_ENTER, Action::kConfirm));
    CHECK(key_is(SDLK_SPACE, Action::kConfirm));
    CHECK(key_is(SDLK_ESCAPE, Action::kBack));
    CHECK(key_is(SDLK_ESCAPE, Action::kPause));
    CHECK_FALSE(key_is(SDLK_SPACE, Action::kBack));
    CHECK_FALSE(key_is(SDLK_ESCAPE, Action::kConfirm));
    CHECK(key_is(SDLK_UP, Action::kUp));
    CHECK(key_is(SDLK_w, Action::kUp));
    CHECK(key_is(SDLK_a, Action::kLeft));
    CHECK(key_is(SDLK_s, Action::kDown));
    CHECK(key_is(SDLK_d, Action::kRight));
    CHECK(key_is(SDLK_LCTRL, Action::kAttack));
    CHECK(key_is(SDLK_F5, Action::kBugReport));
    CHECK(key_is(SDLK_F7, Action::kCheats));
}

// The picker listed its own keys and missed Keypad Enter; it now reads the
// table like every menu.
TEST_CASE("actions: the cheat picker grants on Keypad Enter") {
    CheatPicker p;
    p.open_picker();
    p.handle_key(SDLK_s, [](int) {});
    int granted = -1;
    p.handle_key(SDLK_KP_ENTER, [&granted](int bt) { granted = bt; });
    CHECK(granted == 1);
    CHECK_FALSE(p.open());
}

namespace {

namespace gp = olduvai::presentation::gamepad;

gp::Config nintendo() {
    gp::Config c;
    c.jump = {"b"};       // printed A, right
    c.attack = {"a"};     // printed B, bottom
    c.confirm = {"b"};
    c.back = {"a"};
    c.pause = {"start"};
    return c;
}

SDL_Keycode key(const char* name, gp::Context ctx, const gp::Config& c) {
    return gp::key_for_input(name, ctx, c);
}

}  // namespace

// B attacks in play and backs out of a menu: in play a face button sends
// nothing, so it never opens the pause menu the way Esc would.
TEST_CASE("gamepad: a press means what its context says") {
    const gp::Config n = nintendo();
    const auto play = gp::Context::kPlay, menu = gp::Context::kMenu;
    CHECK(key("a", play, n) == SDLK_UNKNOWN);
    CHECK(key("b", play, n) == SDLK_UNKNOWN);
    CHECK(key("start", play, n) == SDLK_ESCAPE);
    CHECK(key("a", menu, n) == SDLK_ESCAPE);
    CHECK(key("b", menu, n) == SDLK_RETURN);
    CHECK(key("start", menu, n) == SDLK_ESCAPE);
    // Select is free: the hotkey modifier to come.
    CHECK(key("back", menu, n) == SDLK_UNKNOWN);
    // The D-pad navigates in both.
    CHECK(key("dpup", play, n) == SDLK_UP);
    CHECK(key("dpup", menu, n) == SDLK_UP);
    // Xbox: B backs out of menus; in play only Start pauses.
    const gp::Config x;
    CHECK(key("b", play, x) == SDLK_UNKNOWN);
    CHECK(key("b", menu, x) == SDLK_ESCAPE);
    // Either of two buttons, a trigger among them.
    gp::Config two = x;
    two.pause = {"start", "righttrigger"};
    CHECK(key("righttrigger", play, two) == SDLK_ESCAPE);
    CHECK(key("start", play, two) == SDLK_ESCAPE);
}

TEST_CASE("gamepad: a binding setting keeps the names SDL knows") {
    CHECK(gp::inputs_from_string("b,y", {"a"}) ==
          olduvai::presentation::BindingList{"b", "y"});
    CHECK(gp::inputs_from_string("lefttrigger", {"a"}) ==
          olduvai::presentation::BindingList{"lefttrigger"});
    CHECK(gp::inputs_from_string("bogus,x", {"a"}) ==
          olduvai::presentation::BindingList{"x"});
    CHECK(gp::inputs_from_string("bogus", {"a"}) ==
          olduvai::presentation::BindingList{"a"});
    CHECK(gp::inputs_from_string("", {"a"}) ==
          olduvai::presentation::BindingList{"a"});
}

// The player's keyboard bindings move play, never the menus.
TEST_CASE("actions: a key_* binding moves play and leaves menus alone") {
    using olduvai::presentation::apply_key_binding;
    using olduvai::presentation::key_binding;
    using olduvai::presentation::play_key_is;
    SDL_Keysym k{};
    k.scancode = SDL_SCANCODE_P;
    CHECK(key_binding("key_pause") == "Escape");
    REQUIRE(apply_key_binding("key_pause", "P,Escape"));
    CHECK(key_binding("key_pause") == "P,Escape");
    CHECK(play_key_is(k, Action::kPause));
    REQUIRE(apply_key_binding("key_pause", "P"));
    k.scancode = SDL_SCANCODE_ESCAPE;
    CHECK_FALSE(play_key_is(k, Action::kPause));
    CHECK(key_is(SDLK_ESCAPE, Action::kPause));   // the menu's own, fixed
    CHECK_FALSE(apply_key_binding("key_pause", "NoSuchKey"));
    CHECK(key_binding("key_pause") == "P");
    CHECK_FALSE(apply_key_binding("pad_jump", "P"));
    REQUIRE(apply_key_binding("key_pause", ""));   // back to the default
    CHECK(key_binding("key_pause") == "Escape");
    CHECK(play_key_is(k, Action::kPause));
}

// The Quit key joins the rebindable play actions: a default of its own, the
// group rule (no two actions share a key, none is empty) holds with it in, and
// it rebinds and resets like Pause.
TEST_CASE("actions: the Quit key is F10, joins the group and rebinds") {
    using olduvai::presentation::apply_key_binding;
    using olduvai::presentation::BindingList;
    using olduvai::presentation::default_keys;
    using olduvai::presentation::group_ok;
    using olduvai::presentation::kKeyActions;
    using olduvai::presentation::key_binding;
    using olduvai::presentation::play_key_is;

    CHECK(default_keys(Action::kQuit) == BindingList{"F10"});
    std::vector<BindingList> lists;
    for (const auto& k : kKeyActions) lists.push_back(default_keys(k.action));
    std::vector<const BindingList*> group;
    for (const BindingList& l : lists) group.push_back(&l);
    CHECK(group_ok(group));   // F10 collides with no other play action

    SDL_Keysym f10{};
    f10.scancode = SDL_SCANCODE_F10;
    SDL_Keysym q{};
    q.scancode = SDL_SCANCODE_Q;
    apply_key_binding("key_quit", "");
    CHECK(key_binding("key_quit") == "F10");
    CHECK(play_key_is(f10, Action::kQuit));
    REQUIRE(apply_key_binding("key_quit", "Q"));
    CHECK_FALSE(play_key_is(f10, Action::kQuit));
    CHECK(play_key_is(q, Action::kQuit));
    CHECK_FALSE(play_key_is(q, Action::kPause));   // a key stays one action's
    REQUIRE(apply_key_binding("key_quit", ""));
    CHECK(key_binding("key_quit") == "F10");
    CHECK(play_key_is(f10, Action::kQuit));
}

// Select held in play: the shortcuts send their keyboard key, nothing else
// acts, and Select + Start stays free for PortMaster's quit.
TEST_CASE("gamepad: shortcuts fire only with the modifier, only in play") {
    const gp::Config c;
    const auto play = gp::Context::kPlay, menu = gp::Context::kMenu;
    CHECK(gp::key_for_input("rightshoulder", play, c, true) == SDLK_F6);
    CHECK(gp::key_for_input("leftshoulder", play, c, true) == SDLK_F9);
    CHECK(gp::key_for_input("y", play, c, true) == SDLK_F7);
    CHECK(gp::key_for_input("x", play, c, true) == SDLK_F5);
    CHECK(gp::key_for_input("start", play, c, true) == SDLK_UNKNOWN);
    CHECK(gp::key_for_input("back", play, c, true) == SDLK_UNKNOWN);
    CHECK(gp::key_for_input("back", menu, c, false) == SDLK_UNKNOWN);
    // Without it, or in a menu, the buttons are themselves.
    CHECK(gp::key_for_input("rightshoulder", play, c, false) == SDLK_UNKNOWN);
    CHECK(gp::key_for_input("start", play, c, false) == SDLK_ESCAPE);
    CHECK(gp::key_for_input("rightshoulder", menu, c, true) == SDLK_UNKNOWN);
    CHECK(gp::key_for_input("b", menu, c, true) == SDLK_ESCAPE);
    CHECK(key_is(SDLK_F6, Action::kQuicksave));
    CHECK(key_is(SDLK_F9, Action::kQuickload));
}
