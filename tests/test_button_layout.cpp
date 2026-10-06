// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The Button layout view (button_layout.hpp): layouts fold to and from the
// pad_* keys, and a rebind keeps the bindings playable in both contexts.

#include "doctest/doctest.h"
#include "presentation/input/button_layout.hpp"

using olduvai::presentation::bindings_of;
using olduvai::presentation::button_layout_for;
using olduvai::presentation::find_button_layout;
using olduvai::presentation::kButtonLayouts;
using olduvai::presentation::PadBindings;
using olduvai::presentation::playable;
using olduvai::presentation::rebind_action;
using olduvai::presentation::upgrade_round1_nintendo;

namespace {

PadBindings layout(const char* id) { return bindings_of(*find_button_layout(id)); }

}  // namespace

TEST_CASE("every layout folds back to itself and is playable") {
    for (const auto& l : kButtonLayouts) {
        const PadBindings b = bindings_of(l);
        CHECK(button_layout_for(b) == l.id);
        CHECK(playable(b));
    }
    CHECK(find_button_layout("custom") == nullptr);
}

TEST_CASE("xbox is today's defaults; nintendo moves the face buttons") {
    const PadBindings x = layout("xbox");
    CHECK(x.jump == "a");
    CHECK(x.attack == "x");
    CHECK(x.back == "b");
    const PadBindings n = layout("nintendo");
    CHECK(n.jump == "b");       // printed A on a Nintendo-style pad
    CHECK(n.attack == "a");     // printed B
    CHECK(n.confirm == "b");
    CHECK(n.back == "a");       // B backs out of menus and attacks in play
    CHECK(n.pause == "start");
}

TEST_CASE("round 1's nintendo, back on Select, reads as today's") {
    PadBindings b{"b", "a", "b", "back", "start"};
    upgrade_round1_nintendo(b);
    CHECK(button_layout_for(b) == "nintendo");
    // Anything else is left alone, a hand-made Select back included.
    PadBindings c{"a", "x", "a", "back", "start"};
    upgrade_round1_nintendo(c);
    CHECK(c.back == "back");
}

TEST_CASE("back may share a button with attack, never with confirm") {
    CHECK(playable(layout("nintendo")));
    PadBindings b = layout("xbox");
    b.back = b.attack;
    CHECK(playable(b));
    b.back = b.confirm;
    CHECK_FALSE(playable(b));
    b = layout("xbox");
    b.pause = b.attack;
    CHECK_FALSE(playable(b));
}

TEST_CASE("a hand-made mapping reads as custom") {
    PadBindings b = layout("xbox");
    b.attack = "y";
    CHECK(button_layout_for(b) == "custom");
}

TEST_CASE("a clash swaps the two actions instead of refusing") {
    PadBindings b = layout("xbox");   // jump a, attack x, back b, pause start
    REQUIRE(rebind_action(b, "pad_attack", "a"));
    CHECK(b.attack == "a");
    CHECK(b.jump == "x");       // took attack's old button
    CHECK(b.confirm == "x");    // confirm follows jump
    CHECK(playable(b));

    // Back onto jump's button: jump takes back's.
    b = layout("xbox");
    REQUIRE(rebind_action(b, "pad_back", "a"));
    CHECK(b.back == "a");
    CHECK(b.jump == "b");
    CHECK(b.confirm == "b");
    CHECK(playable(b));

    // Nintendo: back onto A, which jumps and confirms.  Jump takes B from
    // back; attack, which also held B, takes A.
    b = layout("nintendo");
    REQUIRE(rebind_action(b, "pad_back", "b"));
    CHECK(b.back == "b");
    CHECK(b.jump == "a");
    CHECK(b.confirm == "a");
    CHECK(b.attack == "b");
    CHECK(playable(b));

    // Nintendo: attack onto A.  Jump takes B, and confirm with it, so back
    // hands B over and takes confirm's old A.
    b = layout("nintendo");
    REQUIRE(rebind_action(b, "pad_attack", "b"));
    CHECK(b.attack == "b");
    CHECK(b.jump == "a");
    CHECK(b.confirm == "a");
    CHECK(b.back == "b");
    CHECK(playable(b));
}

TEST_CASE("every two rebinds from every layout stay playable") {
    const char* keys[] = {"pad_jump", "pad_attack", "pad_back", "pad_pause"};
    const auto buttons = olduvai::presentation::bindable_buttons();
    for (const auto& l : kButtonLayouts)
        for (const char* k1 : keys)
            for (const std::string& b1 : buttons)
                for (const char* k2 : keys)
                    for (const std::string& b2 : buttons) {
                        PadBindings b = bindings_of(l);
                        REQUIRE(rebind_action(b, k1, b1));
                        REQUIRE(rebind_action(b, k2, b2));
                        INFO(l.id, " ", k1, " -> ", b1, ", ", k2, " -> ", b2);
                        CHECK(playable(b));
                        CHECK(b.confirm == b.jump);
                    }
}

TEST_CASE("confirm follows jump, and only the four actions rebind") {
    PadBindings b = layout("nintendo");
    REQUIRE(rebind_action(b, "pad_jump", "y"));
    CHECK(b.confirm == "y");
    CHECK_FALSE(rebind_action(b, "pad_confirm", "x"));
    CHECK_FALSE(rebind_action(b, "music_device", "x"));
    // Rebinding to the same button changes nothing.
    const PadBindings before = b;
    REQUIRE(rebind_action(b, "pad_attack", b.attack));
    CHECK(b.attack == before.attack);
    CHECK(b.jump == before.jump);
}

TEST_CASE("a face button is named as its family prints it, with its position") {
    using olduvai::presentation::pad_button_label;
    using olduvai::presentation::PadFamily;
    CHECK(pad_button_label("a", PadFamily::kXbox) == "A - bottom");
    CHECK(pad_button_label("b", PadFamily::kXbox) == "B - right");
    CHECK(pad_button_label("a", PadFamily::kNintendo) == "B - bottom");
    CHECK(pad_button_label("b", PadFamily::kNintendo) == "A - right");
    CHECK(pad_button_label("x", PadFamily::kNintendo) == "Y - left");
    CHECK(pad_button_label("y", PadFamily::kNintendo) == "X - top");
    CHECK(pad_button_label("a", PadFamily::kPlayStation) == "Cross - bottom");
    CHECK(pad_button_label("y", PadFamily::kPlayStation) == "Triangle - top");
    // The rest keep one name on every pad.
    for (PadFamily f : {PadFamily::kXbox, PadFamily::kNintendo,
                        PadFamily::kPlayStation}) {
        CHECK(pad_button_label("leftshoulder", f) == "L1");
        CHECK(pad_button_label("rightshoulder", f) == "R1");
        CHECK(pad_button_label("back", f) == "Select");
        CHECK(pad_button_label("start", f) == "Start");
    }
}

TEST_CASE("every bindable button has a label that is not its SDL name") {
    using olduvai::presentation::pad_button_label;
    using olduvai::presentation::PadFamily;
    for (const auto& b : olduvai::presentation::bindable_buttons())
        CHECK(pad_button_label(b, PadFamily::kXbox) != b);
}

// ── Two slots per action (§3.34 phase 2) ────────────────────────────────────

using olduvai::presentation::BindingList;
using olduvai::presentation::clear_alternate;
using olduvai::presentation::join_binding;
using olduvai::presentation::rebind_in_group;
using olduvai::presentation::split_binding;

TEST_CASE("a binding setting splits into at most two slots") {
    CHECK(split_binding("b") == BindingList{"b"});
    CHECK(split_binding("b, y") == BindingList{"b", "y"});
    CHECK(split_binding("b,,y,x") == BindingList{"b", "y"});
    CHECK(split_binding("").empty());
    CHECK(join_binding({"Left", "A"}) == "Left,A");
    CHECK(join_binding(split_binding("Left Ctrl,Space")) == "Left Ctrl,Space");
}

TEST_CASE("a group hands a clashing binding the slot's old one") {
    BindingList left{"Left", "A"}, right{"Right", "D"};
    const std::vector<BindingList*> g{&left, &right};
    REQUIRE(rebind_in_group(g, 0, 1, "D"));          // A <-> D
    CHECK(left == BindingList{"Left", "D"});
    CHECK(right == BindingList{"Right", "A"});
    REQUIRE(rebind_in_group(g, 0, 0, "D"));          // its own: slots trade
    CHECK(left == BindingList{"D", "Left"});
    BindingList up{"Up"};
    const std::vector<BindingList*> g2{&up, &right};
    REQUIRE(rebind_in_group(g2, 0, 1, "A"));         // empty slot: taken away
    CHECK(up == BindingList{"Up", "A"});
    CHECK(right == BindingList{"Right"});
    BindingList solo{"Up"}, other{"Right"};
    const std::vector<BindingList*> g3{&solo, &other};
    CHECK_FALSE(rebind_in_group(g3, 1, 1, "Up"));    // would leave solo bare
    CHECK(solo == BindingList{"Up"});
    CHECK(other == BindingList{"Right"});
    CHECK_FALSE(rebind_in_group(g2, 0, 2, "Z"));     // no third slot
}

TEST_CASE("an alternate button joins an action; a clash still swaps") {
    PadBindings b = layout("xbox");
    REQUIRE(rebind_action(b, "pad_attack", "y", 1));
    CHECK(b.attack == "x,y");
    CHECK(button_layout_for(b) == "custom");
    // Jump onto Y: attack's Y takes jump's old A.
    REQUIRE(rebind_action(b, "pad_jump", "y"));
    CHECK(b.jump == "y");
    CHECK(b.attack == "x,a");
    CHECK(b.confirm == "y");
    CHECK(playable(b));
    // An alternate that would strip Pause of its only button is refused.
    const PadBindings before = b;
    CHECK_FALSE(rebind_action(b, "pad_jump", "start", 1));
    CHECK(b.jump == before.jump);
    CHECK(b.pause == before.pause);
    // L2 is a button like any other.
    REQUIRE(rebind_action(b, "pad_jump", "lefttrigger", 1));
    CHECK(b.jump == "y,lefttrigger");
    CHECK(b.confirm == "y,lefttrigger");
}

TEST_CASE("clearing an alternate leaves the primary") {
    PadBindings b = layout("nintendo");
    REQUIRE(rebind_action(b, "pad_jump", "y", 1));
    REQUIRE(clear_alternate(b, "pad_jump"));
    CHECK(b.jump == "b");
    CHECK(b.confirm == "b");
    CHECK_FALSE(clear_alternate(b, "pad_jump"));     // none left
    CHECK_FALSE(clear_alternate(b, "pad_confirm"));
}

TEST_CASE("every two slot picks from every layout stay playable") {
    const char* keys[] = {"pad_jump", "pad_attack", "pad_back", "pad_pause"};
    const auto buttons = olduvai::presentation::bindable_buttons();
    for (const auto& l : kButtonLayouts)
        for (const char* k1 : keys)
            for (int s1 = 0; s1 < 2; ++s1)
                for (const std::string& b1 : buttons) {
                    PadBindings b = bindings_of(l);
                    const bool ok1 = rebind_action(b, k1, b1, s1);
                    // A primary pick from a layout always lands.
                    if (s1 == 0) CHECK(ok1);
                    for (const char* k2 : keys)
                        for (int s2 = 0; s2 < 2; ++s2)
                            for (const std::string& b2 : buttons) {
                                PadBindings c = b;
                                const PadBindings before = c;
                                const bool ok = rebind_action(c, k2, b2, s2);
                                INFO(l.id, " ", k1, "[", s1, "] -> ", b1, ", ",
                                     k2, "[", s2, "] -> ", b2);
                                CHECK(playable(c));
                                CHECK(c.confirm == c.jump);
                                if (!ok) {
                                    CHECK(c.jump == before.jump);
                                    CHECK(c.back == before.back);
                                }
                            }
                }
}
