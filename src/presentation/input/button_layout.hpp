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
// Pad presses reach menus by context (gamepad::Context): in play a face
// button only jumps or attacks, so back may share one with attack.
//
// Pure and header-only: no SDL.
#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

#include "presentation/input/binding_slots.hpp"

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
// confirm on the right face button (printed A), attack and back on the
// bottom one (printed B): back acts only in menus and attack only in play
// (gamepad::Context), so they share it as a console's B does.
inline constexpr ButtonLayout kButtonLayouts[] = {
    {"xbox",     "a", "x", "a", "b", "start"},
    {"nintendo", "b", "a", "b", "a", "start"},
};

// Round 1's nintendo, which had to put back on Select while back also
// paused in play.  A play.json holding it reads as today's nintendo.
inline void upgrade_round1_nintendo(PadBindings& b) {
    if (b.jump == "b" && b.attack == "a" && b.confirm == "b" &&
        b.back == "back" && b.pause == "start")
        b.back = "a";
}

inline constexpr const char* kCustomButtonLayout = "custom";

// Each pad_* key and the PadBindings field it sets, in field order.
struct PadSlot {
    const char* key;
    std::string PadBindings::*field;
};
inline constexpr PadSlot kPadSlots[] = {
    {"pad_jump", &PadBindings::jump},       {"pad_attack", &PadBindings::attack},
    {"pad_confirm", &PadBindings::confirm}, {"pad_back", &PadBindings::back},
    {"pad_pause", &PadBindings::pause},
};

inline bool same_bindings(const PadBindings& a, const PadBindings& b) {
    return std::all_of(std::begin(kPadSlots), std::end(kPadSlots),
                       [&](const PadSlot& s) { return a.*s.field == b.*s.field; });
}

inline PadBindings bindings_of(const ButtonLayout& l) {
    return {l.jump, l.attack, l.confirm, l.back, l.pause};
}

// The layout whose bindings these are, else "custom".
inline std::string button_layout_for(const PadBindings& b) {
    for (const ButtonLayout& l : kButtonLayouts)
        if (same_bindings(b, bindings_of(l))) return l.id;
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
    for (const PadSlot& s : kPadSlots)
        if (key == s.key) return &(b.*s.field);
    return nullptr;
}

// The four actions a player rebinds.  Confirm follows Jump (the button
// that jumps accepts in menus, as in both layouts); a play.json may still
// set pad_confirm apart, which reads as "custom".
inline constexpr const char* kRebindableKeys[] = {"pad_jump", "pad_attack",
                                                  "pad_back", "pad_pause"};

// Whether the bindings play: jump, attack and pause on buttons of their own
// (play), and confirm apart from back (menus).  Back may share a button with
// a play action: it acts only in menus.  Each key may hold two buttons.
inline bool playable(const PadBindings& b) {
    const BindingList jump = split_binding(b.jump);
    const BindingList attack = split_binding(b.attack);
    const BindingList pause = split_binding(b.pause);
    const BindingList back = split_binding(b.back);
    const BindingList confirm = split_binding(b.confirm);
    return group_ok({&jump, &attack, &pause}) && group_ok({&confirm, &back});
}

namespace detail {

// After jump moved: a back button jump now holds takes the one jump gave
// up, or goes when there is none.  False when back would be left empty.
inline bool back_off_jump(BindingList& back, const BindingList& jump_before,
                          const BindingList& jump) {
    std::string given_up;
    for (const std::string& n : jump_before)
        if (!holds(jump, n)) given_up = n;
    for (auto it = back.begin(); it != back.end();) {
        if (!holds(jump, *it)) { ++it; continue; }
        if (!given_up.empty() && !holds(back, given_up)) {
            *it = given_up;
            ++it;
        } else {
            it = back.erase(it);
        }
    }
    return !back.empty();
}

}  // namespace detail

// Bind `button` in `slot` (0 primary, 1 alternate) of `key`, one of
// kRebindableKeys, keeping the bindings playable.  A clash inside a context
// gives the other action this slot's old button, so every primary pick is
// reachable; confirm follows jump.  False, with nothing changed, for any
// other key, or when the pick would leave an action with no button.
inline bool rebind_action(PadBindings& b, const std::string& key,
                          const std::string& button, int slot = 0) {
    BindingList jump = split_binding(b.jump);
    BindingList attack = split_binding(b.attack);
    BindingList pause = split_binding(b.pause);
    BindingList back = split_binding(b.back);
    const BindingList jump_before = jump;
    const std::vector<BindingList*> play = {&jump, &attack, &pause};
    if (key == "pad_back") {
        // Onto a button jump (so confirm) holds: that jump slot takes back's
        // old button, through the play rules.
        const auto j = std::find(jump.begin(), jump.end(), button);
        if (j != jump.end()) {
            const auto s = static_cast<std::size_t>(slot);
            const std::string old = s < back.size() ? back[s] : std::string{};
            if (old.empty() ||
                !rebind_in_group(play, 0, static_cast<int>(j - jump.begin()),
                                 old))
                return false;
        }
        if (!rebind_in_group({&back}, 0, slot, button)) return false;
    } else {
        std::size_t target = play.size();
        if (key == "pad_jump") target = 0;
        if (key == "pad_attack") target = 1;
        if (key == "pad_pause") target = 2;
        if (target == play.size() ||
            !rebind_in_group(play, target, slot, button) ||
            !detail::back_off_jump(back, jump_before, jump))
            return false;
    }
    PadBindings next{join_binding(jump), join_binding(attack),
                     b.confirm, join_binding(back), join_binding(pause)};
    const BindingList confirm = split_binding(b.confirm);
    if (next.jump != b.jump || !group_ok({&confirm, &back}))
        next.confirm = next.jump;
    if (!playable(next)) return false;
    b = next;
    return true;
}

// Empty the alternate slot of `key`.  False for a key with none, or another
// key.
inline bool clear_alternate(PadBindings& b, const std::string& key) {
    std::string* const v = binding_for_key(b, key);
    if (v == nullptr || key == "pad_confirm") return false;
    BindingList l = split_binding(*v);
    if (l.size() < 2) return false;
    l.pop_back();
    *v = join_binding(l);
    if (key == "pad_jump") b.confirm = b.jump;
    return true;
}

// The buttons a binding can hold (SDL's controller names): the triggers are
// axes, read as pressed past half.  Guide is never offered: the firmware
// owns it.
inline std::vector<std::string> bindable_buttons() {
    return {"a", "b", "x", "y", "leftshoulder", "rightshoulder",
            "lefttrigger", "righttrigger", "back", "start", "leftstick",
            "rightstick"};
}

// Which letters a pad prints on its four face buttons.
enum class PadFamily { kXbox, kNintendo, kPlayStation };

// A button name a binding row can hold.
inline bool is_pad_button(const std::string& name) {
    for (const std::string& b : bindable_buttons())
        if (name == b) return true;
    return false;
}

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
    if (button == "lefttrigger") return "L2";
    if (button == "righttrigger") return "R2";
    if (button == "leftstick") return "L3";
    if (button == "rightstick") return "R3";
    if (button == "back") return "Select";
    if (button == "start") return "Start";
    return button;
}

}  // namespace olduvai::presentation
