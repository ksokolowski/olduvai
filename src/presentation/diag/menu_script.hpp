// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OLDUVAI_MENU_SCRIPT for the two LEVEL drivers — the platform level's pause /
// menu / F5-form walk and the boss arena's.  One token per loop iteration,
// consumed before the event poll so a synthetic key reaches that iteration's
// input handling:
//   esc up down left right enter space f5 f7 1..6 bksp del home end tab
//   wait       idle one iteration
//   shot       ask the driver for a composed-frame dump (shot_path, one frame)
//   type:TEXT  typed into the open F5 description editor ('_' = space)
//   stab / ctrlenter   the two modifier chords
//   quit       end the run (so does the end of the script)
// Token parsing and key injection are menu_script_util.hpp (shared with the
// title-menu walk, which keeps its own token loop).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace olduvai::presentation {

class ReportFormService;

struct MenuScript {
    std::vector<std::string> script;
    std::size_t idx = 0;
    int shot_ctr = 0;
    std::string dir = ".";
    std::string shot_path;   // set for one frame when a `shot` token fires
    bool quit = false;

    // Reads OLDUVAI_MENU_SCRIPT / OLDUVAI_MENU_SCRIPT_DIR.
    void load_from_env();
    bool active() const { return !script.empty(); }
};

// Consume one token.  Returns true when the script has ended or asked to
// quit; the CALLER owns the loop exit.  `form` may be null (no type: target).
bool drive_menu_script(MenuScript& ms, ReportFormService* form);

}  // namespace olduvai::presentation
