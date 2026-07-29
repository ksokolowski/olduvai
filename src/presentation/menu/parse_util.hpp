// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Tolerant numeric parsers for settings values (from the user-editable
// play.json / the menu string store). A non-numeric value like "loud" must
// never throw straight through the frame loop — fall back instead.
//
// Hoisted out of game_app.cpp (CC2c) so the pause-settings code can move to its
// own header. Inline: shared by game_app, pause_bindings and the menu flows.

#pragma once

#include <string>

namespace olduvai::presentation {

inline float parse_f(const std::string& s, float fallback) {
    try { return std::stof(s); } catch (...) { return fallback; }
}

inline int parse_i(const std::string& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

}  // namespace olduvai::presentation
