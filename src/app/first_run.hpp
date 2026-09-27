// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// First-run GUI: a double-clicked app has no terminal, so a missing-files
// report on stdout would go nowhere.  Detect a GUI launch and show a native
// dialog with a folder picker and a store link.  SDL2 builds only.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace olduvai::app {

// Probably started from the graphical shell: macOS/Linux no tty on stdin and
// stderr; Windows a console nobody else shares.  OLDUVAI_NO_GUI=1 forces
// false (piped scripts, CI); OLDUVAI_FORCE_GUI=1 forces true.
bool launched_from_gui();

// The Classic/Enhanced question as a standalone box, for installs found
// without the missing-files dialog (GOG auto-discovery, pre-seeded game_dir)
// whose config never answered it.  "hd" or "dos"; OLDUVAI_FIRSTRUN_PRESET
// overrides.
std::string ask_preset_choice();

// Missing/incomplete game dir dialog: what is needed, a folder picker, the
// GOG store page, Quit.  Returns a validated dir (saved as `game_dir`) or
// nullopt on Quit.  `problems`: detect_game_files().problems().
// `chosen_preset` (optional) receives the Style answer as a profile name
// resolved in `family`, for this session (the dialog saves it too).
// Test hooks: OLDUVAI_FIRSTRUN_ANSWER = locate | quit, OLDUVAI_FIRSTRUN_DIR =
// <path>.
std::optional<std::filesystem::path> first_run_dialog(
    const std::filesystem::path& game_dir, const std::string& problems,
    std::string* chosen_preset = nullptr,
    const std::string& family = "desktop");

}  // namespace olduvai::app
