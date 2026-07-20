// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Loading screen, score tally, and palette-fade transitions.
// Loading: "Please Wait" (baseline 0x60) / "while loading Level N" (0x70),
// centred, black screen, mode-2-style fade in/out.   // FUN_270a_0412
// Tally: LEVEL N (y=32) / COMPLETED! (48) / BONUS SCORE (72) and the three
// centred value rows (96/120/144); bonus counts down by 2 (+20 score each),
// lives count down (+1000 each); odd display levels award +1 life first;
// 4-second skippable pauses.                          // FUN_270a_01b4

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "presentation/game_render.hpp"

namespace olduvai::enhance { class HdText; }

namespace olduvai::presentation {

class SdlAudio;

// Presents a frame; returns false to abort (quit requested).
using PresentFn = std::function<bool(const FrameBuffer&)>;
// True when the skip key (attack/fire) is held this moment.
using SkipFn = std::function<bool()>;

// A vector-text row for the loading/tally screens.  `native_baseline_y`
// is the EXE text-row baseline in the 320×200 design space; the present
// implementation draws it at the renderer's OUTPUT resolution (so the glyphs
// are crisp at the physical window size, not the HD compose size).  Colour
// 235,235,235 by default (matches the boss HUD labels).
//
// `align` controls horizontal placement (mirrors the reference's fixed-anchor
// tally layout — labels stay put while the counting digits change width):
//   0 = centred  (default; title rows, loading rows)
//   1 = label    — right-aligned, ending at the tally colon column
//   2 = value    — left-aligned, starting at the tally value column
// Aligns 1/2 are only meaningful inside draw_tally_rows_overlay (which derives
// the colon/value columns from FIXED reference strings, so the columns are the
// same every frame regardless of the current bonus/lives/score digits).
struct HdTextRow {
    int native_baseline_y;
    std::string text;
    int align = 0;
};

// Presents a pre-built HD scene buffer (w×h already at HD resolution) and then
// draws the centred vector-text `rows` at the renderer's OUTPUT resolution as
// a 1:1 overlay over the scene — so the text stays crisp regardless of how far
// SDL scales the scene texture up to the window.  The scene buffer carries NO
// vector text (it is left black / upscaled-only).  Returns false to abort.
using HdPresentFn = std::function<bool(const std::vector<std::uint8_t>&, int w,
                                       int h, const std::vector<HdTextRow>&)>;

// Enhanced-mode handle for the score tally: when set + ok(), the tally routes
// every text row through the cartoon vector font (hd_text) at OUTPUT resolution
// (via the present_hd overlay), mirroring the reference tally/text layers.  Classic
// mode passes a null TallyHd.
struct TallyHd {
    const enhance::HdText* hd_text = nullptr;   // null → classic bitmap path
    int scale = 1;                              // HD target scale (2/3/4)
    HdPresentFn present_hd;                     // uploads scene + text overlay
    // Upscale a native 320×200 RGBA buffer to HD (profile baked into the fn).
    std::function<std::vector<std::uint8_t>(const std::vector<std::uint8_t>&)>
        upscale;
};

constexpr int kFadeFrames = 18;

// Multiply the frame towards black (t = 0 → unchanged, 1 → black).
void apply_fade(FrameBuffer& dst, const FrameBuffer& src, double t);

// Enhanced-mode handle for the loading screen: when set + ok(), the two text
// rows ("Please Wait" / "while loading Level N") are drawn through the cartoon
// vector font (hd_text) at HD resolution on an upscaled black buffer presented
// via present_hd — mirrors the TallyHd path (the reference
// records the loading lines into a TextLayer and flushes them at display res).
// Classic mode passes a default-constructed LoadingHd (null hd_text) and keeps
// the byte-identical bitmap path.  The in/out fades run on the HD buffer.
struct LoadingHd {
    const enhance::HdText* hd_text = nullptr;   // null → classic bitmap path
    int scale = 1;                              // HD target scale (2/3/4)
    HdPresentFn present_hd;                     // uploads the HD buffer
    // Upscale a native 320×200 RGBA buffer to HD (profile baked into the fn).
    std::function<std::vector<std::uint8_t>(const std::vector<std::uint8_t>&)>
        upscale;
};

// Fade `from` to black, then the loading screen in; hold; fade to black.
// Returns false if the user quit.
// `hd` (optional): when hd.hd_text is non-null and ok(), the two text rows are
// rendered via the cartoon vector font at HD resolution (classic bitmap path
// suppressed), and the fades run on the HD buffer — see LoadingHd.
bool show_loading_screen(const FrameBuffer* from, int display_level,
                         const std::vector<formats::Sprite>& charset,
                         const std::vector<formats::Rgb>& pal,
                         const PresentFn& present,
                         const LoadingHd& hd = {});

// Full-screen PC1 (e.g. the game-over picture): fade in, hold, fade out.
// fade_in/fade_out are optional so multi-call holds (the BULLE dream
// screen) can present seamlessly without dipping to black between calls.
bool show_pc1_screen(const formats::Pc1Image& img, int hold_frames,
                     const PresentFn& present, const SkipFn& skip,
                     bool fade_in = true, bool fade_out = true);

// Pure countdown step: apply one bonus-countdown tick or one lives-countdown
// tick to the mutable accumulators.  Returns the new bonus_remaining / lives
// values.  Extracted for headless unit testing (no SDL, no present()).
//
// Bonus phase: bonus_remaining decrements by 2 (clamped to 0), score += 20.
// Lives phase: lives_remaining decrements by 1, score += 1000.
// Call with bonus_remaining > 0 for bonus phase; once it reaches 0 call with
// lives_remaining > 0 for lives phase.
void step_tally_bonus(int& bonus_remaining, long& score);
void step_tally_lives(int& lives_remaining, long& score);

// Enhanced-mode completion-chime handle for the score tally.  When enhanced is
// true and audio is non-null, the tally plays SFX_WAIT_AND_PLAY once at the
// final pause (after both countdowns) — mirrors the reference engine extension
// (`if state.cinematic_cue: audio.play_sfx_event("SFX_WAIT_AND_PLAY")`,
// gated by --enhance cinematic-cue / --enhanced).  The EXE plays NO SFX here
// (FUN_270a_01b4 calls the silent FUN_1847_065a wait), so the default
// (non-enhanced / null audio) path stays silent — EXE-faithful.
struct TallyAudio {
    SdlAudio* audio = nullptr;     // null → no chime
    bool enhanced = false;         // gate: set from --enhance cinematic-cue (or --enhanced)
};

// The level-completion tally.  Mutates score/lives in `state`.
// The two 4-second pauses are edge-triggered: only a fresh SPACE/RETURN
// KEYDOWN (not a key held from gameplay) advances them.  The countdown
// loops themselves are never skippable (EXE FUN_270a_01b4 pacing).
// `hd` (optional): when hd.hd_text is non-null and ok(), the tally renders all
// text rows via the cartoon vector font at HD resolution (classic bitmap path
// suppressed) — see TallyHd.  A default-constructed TallyHd (null hd_text)
// keeps the byte-identical bitmap path.
bool show_score_tally(systems::SystemsState& state, int display_level,
                      int bonus, const std::vector<formats::Sprite>& charset,
                      const std::vector<formats::Rgb>& pal,
                      const PresentFn& present, const SkipFn& skip,
                      const TallyHd& hd = {}, const TallyAudio& sfx = {});

// Boss-level overload: takes lives/score by reference directly (BossPlayerState
// has no SystemsState wrapper).  Same edge-triggered skip semantics.
// EXE: Level_EndScreen(N,500) at 23cf:0fc9 / 24cc:0818 / 254f:0620.
bool show_score_tally(int& lives, long& score, int display_level,
                      int bonus, const std::vector<formats::Sprite>& charset,
                      const std::vector<formats::Rgb>& pal,
                      const PresentFn& present, const SkipFn& skip,
                      const TallyHd& hd = {}, const TallyAudio& sfx = {});

}  // namespace olduvai::presentation
