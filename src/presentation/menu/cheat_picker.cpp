// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/menu/cheat_picker.hpp"

namespace olduvai::presentation {

namespace {
const char* const kPowerupNames[CheatPicker::kRows] = {
    "SPRING", "BOMB", "TIMER", "EXTRA LIFE", "SHIELD", "AXE"};
}  // namespace

const char* CheatPicker::name(int i) {
    return (i >= 0 && i < kRows) ? kPowerupNames[i] : "";
}

std::string CheatPicker::row_label(int i) const {
    return (i == sel_ ? std::string("> ") : std::string("  ")) +
           std::to_string(i + 1) + " " + name(i);
}

bool CheatPicker::handle_key(SDL_Keycode sym,
                             const std::function<void(int)>& grant) {
    if (!open_) return false;
    if (sym == SDLK_ESCAPE || sym == SDLK_F7) {
        open_ = false;
    } else if (sym == SDLK_UP || sym == SDLK_w) {
        sel_ = (sel_ + kRows - 1) % kRows;
    } else if (sym == SDLK_DOWN || sym == SDLK_s) {
        sel_ = (sel_ + 1) % kRows;
    } else if (sym == SDLK_RETURN || sym == SDLK_SPACE) {
        grant(sel_);
        open_ = false;
    } else if (sym >= SDLK_1 && sym <= SDLK_6) {
        grant(static_cast<int>(sym - SDLK_1));
        open_ = false;
    }
    // Consumed either way: the overlay swallows every key while it is up.
    return true;
}

}  // namespace olduvai::presentation
