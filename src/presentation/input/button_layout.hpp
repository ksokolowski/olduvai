// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The "Button layout" choice: which face button does what, as one choice.
// Like the Sound card (sound_card.hpp) it is a VIEW of the settings the pad
// code reads — the five pad_* keys — not a stored key: picking a layout
// writes the five, and five on disk read back as the layout they are, or
// "custom".
//
// Buttons are SDL's POSITIONAL names ("a" bottom, "b" right, "x" left, "y"
// top; gamepad::init makes SDL report every pad that way); a pad's printed
// labels vary.  Xbox pads print A B X Y on those positions; Nintendo-style
// handhelds print B A Y X, so the same layout reads rotated on them, which
// is what the Nintendo layout corrects.
// A player rebinds four actions (kRebindableKeys); confirm follows jump.
//
// Pure and header-only: no SDL.
#pragma once

#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

namespace olduvai::presentation {

// One binding per action, as SDL button names.
struct PadBindings {
    std::string jump;
    std::string attack;
    std::string confirm;   // menus: Enter
    std::string back;      // menus: Esc (also opens the pause menu in play)
    std::string pause;
};

struct ButtonLayout {
    const char* id;
    const char* jump;
    const char* attack;
    const char* confirm;
    const char* back;
    const char* pause;
};

// In menu order.  xbox is the engine default.  nintendo puts jump and
// confirm on the right face button (printed A) and attack on the bottom one
// (printed B); back moves to Select, since B now attacks and back opens the
// pause menu.
inline constexpr ButtonLayout kButtonLayouts[] = {
    {"xbox",     "a", "x", "a", "b",    "start"},
    {"nintendo", "b", "a", "b", "back", "start"},
};

inline constexpr const char* kCustomButtonLayout = "custom";

// The pad_* keys, in PadBindings order.
inline constexpr const char* kPadKeys[] = {"pad_jump", "pad_attack",
                                           "pad_confirm", "pad_back",
                                           "pad_pause"};

inline PadBindings bindings_of(const ButtonLayout& l) {
    return {l.jump, l.attack, l.confirm, l.back, l.pause};
}

// The layout whose bindings these are, else "custom".
inline std::string button_layout_for(const PadBindings& b) {
    for (const ButtonLayout& l : kButtonLayouts)
        if (b.jump == l.jump && b.attack == l.attack &&
            b.confirm == l.confirm && b.back == l.back && b.pause == l.pause)
            return l.id;
    return kCustomButtonLayout;
}

// The layout for an id; nullptr for an unknown id (including "custom").
inline const ButtonLayout* find_button_layout(const std::string& id) {
    for (const ButtonLayout& l : kButtonLayouts)
        if (id == l.id) return &l;
    return nullptr;
}

// The binding a pad_* key names, or nullptr for another key.
inline std::string* binding_for_key(PadBindings& b, const std::string& key) {
    if (key == "pad_jump") return &b.jump;
    if (key == "pad_attack") return &b.attack;
    if (key == "pad_confirm") return &b.confirm;
    if (key == "pad_back") return &b.back;
    if (key == "pad_pause") return &b.pause;
    return nullptr;
}

// The four actions a player rebinds.  Confirm follows Jump (the button
// that jumps accepts in menus, as in both layouts); a play.json may still
// set pad_confirm apart, which reads as "custom".
inline constexpr const char* kRebindableKeys[] = {"pad_jump", "pad_attack",
                                                  "pad_back", "pad_pause"};

// Bind `key` (one of kRebindableKeys) to `button`, keeping the bindings
// playable: jump, attack, back and pause each need their own button (back
// and pause open the pause menu, which on jump or attack would fire on every
// press).  A clash gives the other action this one's old button, so every
// pick is reachable.  False for any other key.
inline bool rebind_action(PadBindings& b, const std::string& key,
                   const std::string& button) {
    std::string* const actions[] = {&b.jump, &b.attack, &b.back, &b.pause};
    std::string* target = nullptr;
    for (std::size_t i = 0; i < std::size(kRebindableKeys); ++i)
        if (key == kRebindableKeys[i]) target = actions[i];
    if (target == nullptr) return false;
    const std::string old = *target;
    const std::string jump_before = b.jump;
    for (std::string* other : actions)
        if (other != target && *other == button) *other = old;
    *target = button;
    // Confirm follows a moved jump, and never shares with back or pause
    // (the pad sends Enter or Esc for a button, not both).
    if (b.jump != jump_before || b.confirm == b.back || b.confirm == b.pause)
        b.confirm = b.jump;
    return true;
}

// The buttons a binding row offers, in its cycle order.
inline std::vector<std::string> bindable_buttons() {
    return {"a", "b", "x", "y", "leftshoulder", "rightshoulder", "back",
            "start"};
}

// Which letters a pad prints on its four face buttons.
enum class PadFamily { kXbox, kNintendo, kPlayStation };

// What a binding row shows for an SDL button name: a face button as its
// printed name and position ("A - right": the game font draws parentheses
// as braces), the rest by their usual names.
inline std::string pad_button_label(const std::string& button, PadFamily f) {
    struct Face {
        const char* name;
        const char* position;
        const char* xbox;
        const char* nintendo;
        const char* playstation;
    };
    static constexpr Face kFaces[] = {
        {"a", "bottom", "A", "B", "Cross"},
        {"b", "right", "B", "A", "Circle"},
        {"x", "left", "X", "Y", "Square"},
        {"y", "top", "Y", "X", "Triangle"},
    };
    for (const Face& face : kFaces) {
        if (button != face.name) continue;
        const char* printed = f == PadFamily::kNintendo      ? face.nintendo
                              : f == PadFamily::kPlayStation ? face.playstation
                                                             : face.xbox;
        return std::string(printed) + " - " + face.position;
    }
    if (button == "leftshoulder") return "L1";
    if (button == "rightshoulder") return "R1";
    if (button == "back") return "Select";
    if (button == "start") return "Start";
    return button;
}

}  // namespace olduvai::presentation
