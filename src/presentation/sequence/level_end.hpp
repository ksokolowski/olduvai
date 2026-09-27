// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The level end, Level_EndScreen(N, 500): the fade, then the score tally.
// The tally is shared by the platform and boss drivers; the platform's fade
// is here, the boss's (the arena and its victory sprites) in boss/.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <SDL.h>

#include "formats/mat.hpp"
#include "presentation/level/level_state.hpp"          // Loaded
#include "presentation/render/game_render.hpp"         // FrameBuffer
#include "presentation/sequence/screen_presenter.hpp"  // ScreenPresenter
#include "presentation/sequence/screens.hpp"           // PresentFn
#include "presentation/sequence/text_screen_present.hpp"

namespace olduvai::presentation {

class SdlAudio;
class WidescreenPresenter;

// What the tally needs.  lives and score are counted into.
struct LevelTally {
    ScreenPresenter& screen;
    const TextScreenDeps& deps;
    bool hd_ok;   // the vector-text tally
    int& lives;
    long& score;
    int display_level;
    const std::vector<formats::Sprite>& charset;
    const std::vector<formats::Rgb>& palette;
    const PresentFn& present;
    SdlAudio* audio;
    const std::filesystem::path& game_dir;
    bool enhanced;
};

// BONUS.MDI, then the tally (the bonus 500).  False = quit: a window close,
// or the OLDUVAI_DUMP_TALLY hook ending the run.
bool play_level_tally(const LevelTally& t);

// What the platform level's ending needs.
struct PlatformEnding {
    Loaded& g;
    WidescreenPresenter& wsp;
    SDL_Window* win;
    FrameBuffer& fb;
    ScreenPresenter& screen;
    const TextScreenDeps& text_screen_deps;
    const PresentFn& present;
    SdlAudio* audio;
    const std::filesystem::path& game_dir;
    bool enhanced;
    std::uint32_t frame_ms;
    bool use_hd_text;
    int display_level;
};


// After the 8b intercept: the fade from the last presented frame, still in
// fb (a re-compose would show the pseudo-exit position the player never saw),
// then the tally.  Fade-dump mode ends the program after the fade: the gate
// has its frames.
bool play_platform_ending(const PlatformEnding& c, int end_px, int end_py);

}  // namespace olduvai::presentation
