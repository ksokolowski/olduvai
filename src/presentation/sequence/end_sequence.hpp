// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// End-of-game screens shown by run_game after the level loop: the game-over
// picture (any death) and — later — the win ending.  Lifted out of game_app.cpp
// as self-contained cinematics; each reads the game files, presents its picture
// with music, and holds until it finishes or the player aborts.
#pragma once

#include <filesystem>
#include <string>

#include "presentation/audio/audio.hpp"         // SdlAudio
#include "presentation/window_util.hpp"   // ScaledWindow

namespace olduvai::presentation {

// Game-over screen (EXE FUN_2bd7_02e7): MORT.MDI death music over THEEND.PC1,
// held ~8 s (ESC/QUIT aborts early), then a gradual fade-out.  Shown for ANY
// game-over — boss or platform death.  THEEND.PC1 is in FILESA.VGA, MORT.MDI in
// FILESA.CUR (read from `game_dir`).  `hd_scale`/`hd_profile` drive the upscale.
void show_game_over_screen(const std::filesystem::path& game_dir,
                           SdlAudio& audio, ScaledWindow& sw, int hd_scale,
                           const std::string& hd_profile);

// Win ending (EXE Game_WinSequence FUN_2bd7_0183): FIN.MDI over COOL3.PC1 with
// the COOL2.MAT caveman rising y=198→73 at -2/frame (smooth-motion interpolates
// the scroll), then holds for a key press+release.  Missing assets → silent
// return (§F6).  OLDUVAI_ENDING_SHOT dumps the first frame + sets
// `quit_requested` (headless verify).  `hd_scale`/`hd_profile` drive the upscale.
void show_win_ending(const std::filesystem::path& game_dir, SdlAudio& audio,
                     ScaledWindow& sw, int hd_scale,
                     const std::string& hd_profile, bool smooth_motion,
                     bool& quit_requested);

}  // namespace olduvai::presentation
