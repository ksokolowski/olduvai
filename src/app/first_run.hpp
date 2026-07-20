// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// First-run GUI experience.  A double-clicked app (Finder / Explorer / file
// manager) has no terminal: the missing-game-files report printed to stdout
// goes nowhere and the app appears to "do nothing".  This module detects a
// GUI launch and turns that failure into a native dialog with a folder
// picker and a store link.  Compiled only when SDL2 is available.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace olduvai::app {

// True when the process was (most likely) started from the graphical shell
// rather than a terminal — the case where stdout is invisible and errors
// must be dialogs.  macOS/Linux: no tty on stdin+stderr.  Windows: the
// process owns a console nobody else shares (fresh console = double-click).
// OLDUVAI_NO_GUI=1 forces false (scripts/CI piping stdio);
// OLDUVAI_FORCE_GUI=1 forces true (see the real dialogs from a terminal).
bool launched_from_gui();

// Dialog loop for a missing/incomplete game dir: explains what Olduvai
// needs, offers a native folder picker, the GOG store page, and Quit.
// Returns a VALIDATED game dir (already persisted as `game_dir` in
// play.json) or nullopt when the user quits.  `problems` = the
// detect_game_files().problems() text for the rejected dir.
//
// Test hooks (env; skip the real UI):
//   OLDUVAI_FIRSTRUN_ANSWER = locate | quit   — forced button choice
//   OLDUVAI_FIRSTRUN_DIR    = <path>          — forced picker result
// The one-time "Classic DOS or Enhanced HD?" question as a standalone box
// — for installs found WITHOUT the missing-files dialog (GOG auto-discovery
// / pre-seeded game_dir), asked when the saved config has never answered
// it.  Returns "hd" or "dos".  OLDUVAI_FIRSTRUN_PRESET overrides (tests).
std::string ask_preset_choice();

// On a validated Locate, `chosen_preset` (when non-null) receives the
// presentation choice ("hd" / "dos") so the CALLING session can adopt it
// too — the dialog itself only persists it to play.json for future runs.
std::optional<std::filesystem::path> first_run_dialog(
    const std::filesystem::path& game_dir, const std::string& problems,
    std::string* chosen_preset = nullptr);

}  // namespace olduvai::app
