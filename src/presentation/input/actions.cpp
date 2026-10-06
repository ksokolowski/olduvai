// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/input/actions.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "presentation/game_app.hpp"   // GameOptions

namespace olduvai::presentation {

namespace {

// Names resolve once per change.  A single-character name resolves to its
// own keycode whatever the keyboard layout (as SDLK_w always did); held
// state and play presses read scancodes, the key's position.
struct Table {
    std::array<BindingList, kActionCount> names;
    std::array<std::vector<SDL_Keycode>, kActionCount> syms;
    std::array<std::vector<SDL_Scancode>, kActionCount> scans;

    void set(std::size_t i, const BindingList& n) {
        names[i] = n;
        syms[i].clear();
        scans[i].clear();
        for (const std::string& name : n) {
            syms[i].push_back(SDL_GetKeyFromName(name.c_str()));
            scans[i].push_back(SDL_GetScancodeFromName(name.c_str()));
        }
    }
};

Table make_defaults() {
    Table t;
    for (std::size_t i = 0; i < kActionCount; ++i)
        t.set(i, default_keys(static_cast<Action>(i)));
    return t;
}

const Table& defaults() {
    static const Table t = make_defaults();
    return t;
}

Table& live() {
    static Table t = make_defaults();
    return t;
}

// The kKeyActions entry for a key_* setting, or nullptr.
const KeyAction* key_action(const std::string& key) {
    for (const KeyAction& k : kKeyActions)
        if (key == k.key) return &k;
    return nullptr;
}

std::size_t index_of(Action a) { return static_cast<std::size_t>(a); }

}  // namespace

bool key_is(SDL_Keycode sym, Action a) {
    const auto& syms = defaults().syms[index_of(a)];
    return std::find(syms.begin(), syms.end(), sym) != syms.end();
}

bool play_key_is(const SDL_Keysym& key, Action a) {
    const auto& scans = live().scans[index_of(a)];
    return std::find(scans.begin(), scans.end(), key.scancode) != scans.end();
}

bool key_held(Action a) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys == nullptr) return false;
    const auto& scans = live().scans[index_of(a)];
    return std::any_of(scans.begin(), scans.end(),
                       [keys](SDL_Scancode s) { return keys[s] != 0; });
}

void init_key_bindings(const GameOptions& opts) {
    const std::string* const values[] = {&opts.key_left, &opts.key_right,
                                         &opts.key_up,   &opts.key_down,
                                         &opts.key_attack, &opts.key_pause,
                                         &opts.key_quit};
    static_assert(std::size(values) == std::size(kKeyActions),
                  "one GameOptions field per kKeyActions entry, in its order");
    for (std::size_t i = 0; i < std::size(kKeyActions); ++i)
        if (!apply_key_binding(kKeyActions[i].key, *values[i]))
            apply_key_binding(kKeyActions[i].key, "");
}

std::string key_binding(const std::string& key) {
    const KeyAction* k = key_action(key);
    return k == nullptr ? std::string{}
                        : join_binding(live().names[index_of(k->action)]);
}

bool apply_key_binding(const std::string& key, const std::string& value) {
    const KeyAction* k = key_action(key);
    if (k == nullptr) return false;
    if (value.empty()) {
        live().set(index_of(k->action), default_keys(k->action));
        return true;
    }
    const BindingList names =
        valid_bindings(value, [](const std::string& n) {
            return SDL_GetScancodeFromName(n.c_str()) != SDL_SCANCODE_UNKNOWN;
        });
    if (names.empty()) return false;
    live().set(index_of(k->action), names);
    return true;
}

}  // namespace olduvai::presentation
