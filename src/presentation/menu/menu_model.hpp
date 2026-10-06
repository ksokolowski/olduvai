// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Loader for the declarative menu model (data/menus.json) into a MenuModel.
// (objects + arrays), so this ships a small self-contained recursive-descent
// JSON parser.

#pragma once

#include <algorithm>
#include <optional>
#include <string>

#include "presentation/input/button_layout.hpp"   // pad_button_label
#include "presentation/menu/menu.hpp"

namespace olduvai::presentation {

// The menu model, generated from assets/data/menus.json at BUILD time by
// cmake/gen_menu_model.cmake.  There is no runtime JSON: the build already
// read the file, so parsing it again in the shipped binary was work the build
// had done and then handed back to the user's machine.
//
// menus.json remains the authoring format and the place a future translation
// set would live — it is a build input, not a runtime input.  Editing it
// requires a rebuild, and a malformed edit fails that build.
MenuModel built_in_menu_model();

// The button rows name each button as `f` prints it: "A - right".  A row is
// a button row when every value is a pad button, not by its key: a later
// pad_* setting holding anything else keeps its own labels.
inline void label_pad_rows(MenuModel& m, PadFamily f) {
    for (auto& [id, screen] : m.screens) {
        for (MenuItem& it : screen.items) {
            if (it.values.empty() ||
                !std::all_of(it.values.begin(), it.values.end(), is_pad_button))
                continue;
            it.value_labels.resize(it.values.size());
            for (std::size_t i = 0; i < it.values.size(); ++i)
                it.value_labels[i] = pad_button_label(it.values[i], f);
        }
    }
}

}  // namespace olduvai::presentation
