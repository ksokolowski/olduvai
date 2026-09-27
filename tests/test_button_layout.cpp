// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The Button layout view (button_layout.hpp): layouts fold to and from the
// pad_* keys, and a rebind keeps every action on a button of its own.

#include "doctest/doctest.h"
#include "presentation/input/button_layout.hpp"

using olduvai::presentation::bindings_of;
using olduvai::presentation::button_layout_for;
using olduvai::presentation::find_button_layout;
using olduvai::presentation::kButtonLayouts;
using olduvai::presentation::PadBindings;
using olduvai::presentation::rebind_action;

namespace {

PadBindings layout(const char* id) { return bindings_of(*find_button_layout(id)); }

// jump, attack, back and pause on four different buttons; confirm off back
// and pause.
bool playable(const PadBindings& b) {
    const std::string a[] = {b.jump, b.attack, b.back, b.pause};
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            if (a[i] == a[j]) return false;
    return b.confirm != b.back && b.confirm != b.pause;
}

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
    CHECK(n.back == "back");    // Select: B attacks now
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
}

TEST_CASE("every single rebind from every layout stays playable") {
    const char* keys[] = {"pad_jump", "pad_attack", "pad_back", "pad_pause"};
    for (const auto& l : kButtonLayouts)
        for (const char* k : keys)
            for (const std::string& btn :
                 olduvai::presentation::bindable_buttons()) {
                PadBindings b = bindings_of(l);
                REQUIRE(rebind_action(b, k, btn));
                INFO(l.id, " ", k, " -> ", btn);
                CHECK(playable(b));
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
