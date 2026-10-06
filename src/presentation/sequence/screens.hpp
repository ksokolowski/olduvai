// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Loading screen, score tally, palette fades.
// Loading: "Please Wait" (baseline 0x60) / "while loading Level N" (0x70),
// centred on black, fade in/out.   // FUN_270a_0412
// Tally: LEVEL N (y=32) / COMPLETED! (48) / BONUS SCORE (72) and three centred
// value rows (96/120/144); bonus counts down by 2 (+20 score each), lives by 1
// (+1000 each); odd display levels award +1 life first; 4-second skippable
// pauses.                          // FUN_270a_01b4

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "presentation/render/game_render.hpp"

namespace olduvai::enhance { class HdText; }

namespace olduvai::presentation {

class SdlAudio;

// Presents a frame; returns false to abort (quit requested).
using PresentFn = std::function<bool(const FrameBuffer&)>;
// True when the skip key (attack/fire) is held this moment.
using SkipFn = std::function<bool()>;

// A vector-text row for the loading/tally screens, drawn at output resolution.
// `native_baseline_y` is the EXE baseline in 320x200 space.  Colour
// 235,235,235 (the boss HUD labels).
struct HdTextRow {
    int native_baseline_y;
    std::string text;
    // 0 centred; 1 label, right-aligned to the colon column; 2 value,
    // left-aligned after it; 3 value-column width reservation (measured, never
    // drawn).  1-3 only in draw_tally_rows_overlay, which derives the columns
    // from fixed strings so they do not move as digits change.
    int align = 0;
};

// Present a pre-built HD scene (w x h) and draw `rows` over it at output
// resolution.  The scene has no vector text; an empty scene means black.
// Returns false to abort.
using HdPresentFn = std::function<bool(const std::vector<std::uint8_t>&, int w,
                                       int h, const std::vector<HdTextRow>&)>;

// Enhanced text screens (loading card, tally): with hd_text set and ok(), every
// row goes through the vector font at output resolution.  Default (null
// hd_text): the classic bitmap path.
struct TextScreenHd {
    const enhance::HdText* hd_text = nullptr;   // null → classic bitmap path
    int scale = 1;                              // HD target scale (2/3/4)
    HdPresentFn present_hd;                     // uploads scene + text overlay
    // Upscale a native 320x200 RGBA buffer to HD (profile baked into the fn).
    std::function<std::vector<std::uint8_t>(const std::vector<std::uint8_t>&)>
        upscale;
};

constexpr int kFadeFrames = 18;

// Multiply the frame towards black (t = 0 → unchanged, 1 → black).
void apply_fade(FrameBuffer& dst, const FrameBuffer& src, double t);
// The same over any RGBA buffer (an HD frame); `dst` takes src's size.
void fade_rgba(std::vector<std::uint8_t>& dst,
               const std::vector<std::uint8_t>& src, double t);

// Fade `from` to black over `frames` + 1 frames (the last fully black).
// `on_frame` sees each frame before it is presented.  Returns false when
// `present` does.
bool fade_to_black(const FrameBuffer& from, const PresentFn& present,
                   const std::function<void(const FrameBuffer&)>& on_frame = {},
                   int frames = kFadeFrames);

// How a text screen draws: the level's bitmap font in its palette, the
// present, and the vector text when HD (TextScreenHd; default: bitmap).
struct TextPage {
    const std::vector<formats::Sprite>& charset;
    const std::vector<formats::Rgb>& palette;
    const PresentFn& present;
    TextScreenHd hd;
};

// Fade `from` to black, the loading screen in, hold, fade out.  Returns false
// if the user quit.
bool show_loading_screen(const FrameBuffer* from, int display_level,
                         const TextPage& page);

// Full-screen PC1 (e.g. the game-over picture): fade in, hold, fade out.  The
// fades are optional so consecutive holds (the BULLE dream screen) do not dip
// to black.
bool show_pc1_screen(const formats::Pc1Image& img, int hold_frames,
                     const PresentFn& present, const SkipFn& skip,
                     bool fade_in = true, bool fade_out = true);

// One countdown tick.  Bonus: -2 (clamped at 0), score +20.  Lives: -1, score
// +1000.  Run the bonus phase to 0, then the lives phase.
void step_tally_bonus(int& bonus_remaining, long& score);
void step_tally_lives(int& lives_remaining, long& score);

// Enhanced completion chime: SFX_WAIT_AND_PLAY once at the final pause (the
// reference's extension).  The EXE plays nothing here (FUN_270a_01b4 calls the
// silent wait FUN_1847_065a), so classic / null audio stays silent.
struct TallyAudio {
    SdlAudio* audio = nullptr;     // null → no chime
    bool enhanced = false;         // gate: set from --enhanced
};

// Level-completion tally for both drivers (Level_EndScreen(N,500):
// FUN_270a_01b4, called from 21f3:082b / 2276:0ea4 / 2361:06d6 / 25b2:0892 and
// the boss exits 23cf:0fc9 / 24cc:0818 / 254f:0620).  Mutates `lives` and
// `score`; runs after the level loop.  The two 4-second pauses advance only on
// a fresh SPACE/RETURN keydown; the countdowns are never skippable.  Returns
// false when quit (window close).
bool show_score_tally(int& lives, long& score, int display_level, int bonus,
                      const TextPage& page, const TallyAudio& sfx = {});


}  // namespace olduvai::presentation
