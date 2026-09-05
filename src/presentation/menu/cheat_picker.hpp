// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// CheatPicker — the --cheats power-up overlay (F7 opens; UP/DOWN select;
// ENTER/SPACE or 1-6 grant; ESC or F7 closes).  Pauses the world while open.
//
// BACKLOG §3.7 cluster 2.  The pause cluster turned out to be almost entirely
// owned already — `PauseService` and `ReportFormService` are real types, and
// `god_active` is a session flag rather than menu state.  What had no owner was
// this: two locals, a names table, a shared row-label lambda, and a seven-branch
// key block, all loose in `run_platform_level`.
//
// The row label is here rather than at the draw sites because BOTH of them —
// the HD vector overlay and the classic bitmap path — must render the same
// string, or the two look different for the same selection.
#pragma once

#include <functional>
#include <string>

#include <SDL.h>

namespace olduvai::presentation {

class CheatPicker {
public:
    static constexpr int kRows = 6;

    bool open() const { return open_; }
    int sel() const { return sel_; }
    void open_picker() {
        open_ = true;
        sel_ = 0;
    }
    void close() { open_ = false; }

    // "> N NAME" for the selected row (N is the 1-6 hotkey), "  N NAME"
    // otherwise — the number is advertised so the hotkeys are discoverable.
    std::string row_label(int i) const;

    // Handle one key while the picker is up.  Returns true when the picker
    // consumed it, which is ALWAYS while open: the overlay swallows every key
    // it does not act on, so a stray press cannot reach gameplay behind it.
    // `grant(bonus_type)` applies the pick; the picker closes itself after.
    bool handle_key(SDL_Keycode sym, const std::function<void(int)>& grant);

    // The bonus type behind row `i` (the picker's rows are bonus types 0-5).
    static const char* name(int i);

private:
    bool open_ = false;
    int sel_ = 0;
};

}  // namespace olduvai::presentation
