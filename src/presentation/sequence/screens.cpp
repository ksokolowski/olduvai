// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "enhance/parallel_rows.hpp"
#include "presentation/sequence/screens.hpp"

#include "presentation/render/shift_blit.hpp"   // clear_opaque

#include <SDL.h>
#include <algorithm>
#include <cstdio>

#include "enhance/hd_text.hpp"
#include "presentation/audio/audio.hpp"
#include "presentation/render/hud_render.hpp"

namespace olduvai::presentation {

namespace {

constexpr int kTallyPauseFrames = 4 * 18;   // 4 seconds at 18 Hz

// Centred bitmap text: fixed 8 px/char metric (the original centring).
void draw_centered(FrameBuffer& fb,
                   const std::vector<formats::Sprite>& charset,
                   const std::vector<formats::Rgb>& pal, int baseline_y,
                   const std::string& text) {
    const int x = 160 - static_cast<int>(text.size()) * 8 / 2;
    draw_text(fb, charset, pal, x, baseline_y, text);
}

// What one poll of the tally's input means.  Every waiting loop on this screen
// asks the same question, so it is asked in one place.
enum class TallyKey { None, Skip, Quit };

// Drain the event queue and classify, edge-triggered (a fresh KEYDOWN only), so
// a key held from gameplay does not skip the pause (the EXE polls the fire key,
// FUN_1847_0670).  Quit is a window close only; ESC skips like SPACE/RETURN (a
// post-win tally must never abort).
TallyKey poll_tally_key() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return TallyKey::Quit;
        if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) return TallyKey::Skip;
            // Alt+Enter is the fullscreen chord, not a skip: requeue it for the
            // present path's poll and stop draining.
            if ((ev.key.keysym.sym == SDLK_RETURN ||
                 ev.key.keysym.sym == SDLK_KP_ENTER) &&
                (ev.key.keysym.mod & KMOD_ALT) != 0) {
                SDL_PushEvent(&ev);
                break;
            }
            if (ev.key.keysym.sym == SDLK_SPACE ||
                ev.key.keysym.sym == SDLK_RETURN) {
                return TallyKey::Skip;   // fresh key-down → skip
            }
        }
    }
    return TallyKey::None;
}

// Wait up to `frames` at 18 Hz on a static bitmap frame, returning early on a
// skip.  Returns false only on quit (window close).
bool tally_pause(const PresentFn& present, const FrameBuffer& fb,
                 int frames) {
    for (int f = 0; f < frames; ++f) {
        const TallyKey k = poll_tally_key();
        if (k == TallyKey::Quit) return false;
        if (k == TallyKey::Skip) return true;
        if (!present(fb)) return false;
    }
    return true;
}

}  // namespace

// ── Pure countdown helpers (headless-testable; no SDL, no present) ──────────

void step_tally_bonus(int& bonus_remaining, long& score) {
    // EXE FUN_270a_01b4 at 0x0303-0x0360: bonus -= 2 (clamped), score += 20.
    bonus_remaining -= 2;
    if (bonus_remaining < 0) bonus_remaining = 0;
    score += 20;
}

void step_tally_lives(int& lives_remaining, long& score) {
    // EXE FUN_270a_01b4 at 0x0367-0x03bd: lives -= 1, score += 1000.
    --lives_remaining;
    score += 1000;
}

// ── apply_fade ───────────────────────────────────────────────────────────────

// Scale a buffer toward black: the main per-frame transition cost on a
// Cortex-A53.  __restrict pointers let the compiler vectorise (0 -> 45 NEON
// instructions); parallel_rows uses the idle cores; the buffer is the HD one
// (2.4 MB each way per frame at widescreen scale 3).  Each band owns a
// disjoint byte range, so the result is identical however rows split.
void apply_fade(FrameBuffer& dst, const FrameBuffer& src, double t) {
    const int mul = static_cast<int>((1.0 - t) * 256.0);
    const std::size_t n = src.px.size();
    if (n == 0) return;
    std::uint8_t* const d0 = dst.px.data();
    const std::uint8_t* const s0 = src.px.data();
    // Bands are counted in PIXELS so a band boundary can never fall inside one.
    const int pixels = static_cast<int>(n / 4);
    // Capture by value and re-qualify inside: pointers captured by reference
    // lose __restrict at the std::function boundary (verified in the aarch64
    // disassembly).
    enhance::parallel_rows(pixels, [d0, s0, mul](int p0, int p1) {
        std::uint8_t* __restrict d = d0;
        const std::uint8_t* __restrict s = s0;
        for (std::size_t i = static_cast<std::size_t>(p0) * 4,
                         e = static_cast<std::size_t>(p1) * 4;
             i < e; i += 4) {
            d[i] = static_cast<std::uint8_t>(s[i] * mul >> 8);
            d[i + 1] = static_cast<std::uint8_t>(s[i + 1] * mul >> 8);
            d[i + 2] = static_cast<std::uint8_t>(s[i + 2] * mul >> 8);
            d[i + 3] = 255;
        }
    });
}

bool fade_to_black(const FrameBuffer& from, const PresentFn& present,
                   const std::function<void(const FrameBuffer&)>& on_frame) {
    FrameBuffer work{from.w, from.h};
    for (int f = 0; f <= kFadeFrames; ++f) {
        apply_fade(work, from, static_cast<double>(f) / kFadeFrames);
        if (on_frame) on_frame(work);
        if (!present(work)) return false;
    }
    return true;
}

// ── show_loading_screen ──────────────────────────────────────────────────────

bool show_loading_screen(const FrameBuffer* from, int display_level,
                         const TextPage& page) {
    const TextScreenHd& hd = page.hd;
    char line2[40];
    std::snprintf(line2, sizeof line2, "while loading Level %d",
                  display_level);

    // Enhanced: the two rows in the vector font over an upscaled black buffer,
    // via hd.present_hd.  Null hd_text: the classic bitmap path.
    const bool hd_on = hd.hd_text != nullptr && hd.hd_text->ok() &&
                       hd.present_hd && hd.upscale;

    if (hd_on) {
        const int hw = 320 * hd.scale;
        const int hh = 200 * hd.scale;
        // The two rows at the EXE baselines (0x60/0x70), drawn at output
        // resolution; the scene buffer has no text.
        const std::vector<HdTextRow> rows = {{0x60, "Please Wait"},
                                             {0x70, line2}};
        const std::vector<HdTextRow> no_rows;
        FrameBuffer black;   // native black (alpha=255), no bitmap text
        for (std::size_t i = 3; i < black.px.size(); i += 4) black.px[i] = 255;
        const std::vector<std::uint8_t> loading_hd = hd.upscale(black.px);
        // Upscale the native "from" frame once, as the tally does its base.
        std::vector<std::uint8_t> from_hd;
        if (from != nullptr) from_hd = hd.upscale(from->px);

        // Fade an HD RGBA buffer towards black (t=0 unchanged, 1 black) into
        // `out`.  Same 8-bit multiply as apply_fade, applied per HD pixel.
        std::vector<std::uint8_t> work_hd(loading_hd.size());
        auto fade_hd = [&](const std::vector<std::uint8_t>& src, double t) {
            const int mul = static_cast<int>((1.0 - t) * 256.0);
            for (std::size_t i = 0; i < src.size(); i += 4) {
                work_hd[i] =
                    static_cast<std::uint8_t>(src[i] * mul >> 8);
                work_hd[i + 1] =
                    static_cast<std::uint8_t>(src[i + 1] * mul >> 8);
                work_hd[i + 2] =
                    static_cast<std::uint8_t>(src[i + 2] * mul >> 8);
                work_hd[i + 3] = 255;
            }
        };

        if (from != nullptr) {              // fade current → black (no text)
            for (int f = 0; f <= kFadeFrames; ++f) {
                fade_hd(from_hd, static_cast<double>(f) / kFadeFrames);
                if (!hd.present_hd(work_hd, hw, hh, no_rows)) return false;
            }
        }
        // The loading text overlay is full-opacity (drawn at output res); the
        // scene fades behind it, so the rows are shown on every loading frame.
        for (int f = kFadeFrames; f >= 0; --f) {   // fade in the background
            fade_hd(loading_hd, static_cast<double>(f) / kFadeFrames);
            if (!hd.present_hd(work_hd, hw, hh, rows)) return false;
        }
        for (int f = 0; f < 9; ++f) {              // hold ~0.5 s
            if (!hd.present_hd(loading_hd, hw, hh, rows)) return false;
        }
        for (int f = 0; f <= kFadeFrames; ++f) {   // fade out the background
            fade_hd(loading_hd, static_cast<double>(f) / kFadeFrames);
            if (!hd.present_hd(work_hd, hw, hh, rows)) return false;
        }
        return true;
    }

    // ── Classic bitmap path (byte-identical) ──
    FrameBuffer loading;
    for (std::size_t i = 3; i < loading.px.size(); i += 4) loading.px[i] = 255;
    draw_centered(loading, page.charset, page.palette, 0x60, "Please Wait");
    draw_centered(loading, page.charset, page.palette, 0x70, line2);

    if (from != nullptr && !fade_to_black(*from, page.present)) return false;
    FrameBuffer work;
    for (int f = kFadeFrames; f >= 0; --f) {   // fade in the loading text
        apply_fade(work, loading, static_cast<double>(f) / kFadeFrames);
        if (!page.present(work)) return false;
    }
    for (int f = 0; f < 9; ++f) {              // hold ~0.5 s
        if (!page.present(loading)) return false;
    }
    return fade_to_black(loading, page.present);
}

// ── show_pc1_screen ──────────────────────────────────────────────────────────

bool show_pc1_screen(const formats::Pc1Image& img, int hold_frames,
                     const PresentFn& present, const SkipFn& skip,
                     bool fade_in, bool fade_out) {
    const FrameBuffer fb = pc1_frame(img);
    FrameBuffer work;
    if (fade_in) {
        for (int f = kFadeFrames; f >= 0; --f) {
            apply_fade(work, fb, static_cast<double>(f) / kFadeFrames);
            if (!present(work)) return false;
        }
    }
    for (int f = 0; f < hold_frames; ++f) {
        if (!present(fb)) return false;
        if (skip && skip()) break;
    }
    return !fade_out || fade_to_black(fb, present);
}

// ---- show_score_tally ----

namespace {

// The level-end tally: LEVEL n / COMPLETED! / BONUS SCORE, then the bonus
// and lives rows counting into the score.  Classic draws bitmap rows into a
// native frame; enhanced sends vector rows over a black upscaled base (the
// reference's text layer).
class ScoreTally {
public:
    ScoreTally(int& lives, long& score, int display_level, int bonus,
               const TextPage& page)
        : score_(score), level_(display_level), bonus_(bonus), lives_(lives),
          charset_(page.charset), pal_(page.palette), present_(page.present),
          hd_(page.hd),
          hd_on_(hd_.hd_text != nullptr && hd_.hd_text->ok() &&
                 hd_.present_hd && hd_.upscale),
          // The value columns' widths: the starting counts with every digit
          // '8' (the widest), since the counts only fall.
          reserve_bonus_(widest(bonus, "  x  10")),
          reserve_lives_(widest(lives, "  x  1000")) {}

    // One frame of the current counts.
    bool show() {
        if (!hd_on_) {
            draw_bitmap();
            return present_(fb_);
        }
        return hd_.present_hd(black_, 320 * hd_.scale, 200 * hd_.scale, rows());
    }

    // Hold up to `frames`, re-presenting; a fresh SPACE/RETURN skips.
    bool hold(int frames) {
        if (!hd_on_) return tally_pause(present_, fb_, frames);
        const std::vector<HdTextRow> r = rows();
        for (int f = 0; f < frames; ++f) {
            const TallyKey k = poll_tally_key();
            if (k == TallyKey::Quit) return false;
            if (k == TallyKey::Skip) return true;
            if (!hd_.present_hd(black_, 320 * hd_.scale, 200 * hd_.scale, r))
                return false;
        }
        return true;
    }

    // The bonus (-2, +20 a frame), then the lives (-1, +1000).  SPACE/RETURN
    // fast-forwards the same per-step arithmetic (the same final score), an
    // enhanced skip the EXE cannot do (FUN_270a_01b4 0x0303-0x03bd: vsync
    // loops, no input poll).  False on a window close.
    bool count() {
        return count_down(bonus_, step_tally_bonus) &&
               count_down(lives_, step_tally_lives);
    }

private:
    static std::string widest(int v, const char* suffix) {
        std::string out = std::to_string(v);
        for (char& c : out)
            if (c >= '0' && c <= '9') c = '8';
        return out + suffix;
    }

    bool count_down(int& remaining, void (*step)(int&, long&)) {
        while (remaining > 0) {
            step(remaining, score_);
            if (!show()) return false;
            const TallyKey k = poll_tally_key();
            if (k == TallyKey::Quit) return false;
            if (k == TallyKey::Skip) {
                while (bonus_ > 0) step_tally_bonus(bonus_, score_);
                while (lives_ > 0) step_tally_lives(lives_, score_);
                return true;
            }
        }
        return true;
    }

    // Classic: centred rows, the EXE's "%6d" padding.
    void draw_bitmap() {
        clear_opaque(fb_.px);
        char buf[40];
        std::snprintf(buf, sizeof buf, "LEVEL %d", level_);
        draw_centered(fb_, charset_, pal_, 32, buf);
        draw_centered(fb_, charset_, pal_, 48, "COMPLETED!");
        draw_centered(fb_, charset_, pal_, 72, "BONUS SCORE");
        std::snprintf(buf, sizeof buf, "BONUS : %6d  x  10  ", bonus_);
        draw_centered(fb_, charset_, pal_, 96, buf);
        std::snprintf(buf, sizeof buf, "LIFE  : %6d  x  1000", lives_);
        draw_centered(fb_, charset_, pal_, 120, buf);
        std::snprintf(buf, sizeof buf, "SCORE : %06ld", std::min(score_, 999999L));
        draw_centered(fb_, charset_, pal_, 144, buf);
    }

    // Enhanced rows at the EXE baselines.  Titles centred (align 0); the
    // counting rows split into a right-aligned label (1) and a left-aligned
    // value (2) so the proportional font does not slide as digits change.
    // Raw numbers, no padding, as the reference.  Align 3 reserves a value
    // column's width (measured, never drawn).
    std::vector<HdTextRow> rows() const {
        char buf[40];
        std::vector<HdTextRow> r;
        std::snprintf(buf, sizeof buf, "LEVEL %d", level_);
        r.push_back({32, buf, 0});
        r.push_back({48, "COMPLETED!", 0});
        r.push_back({72, "BONUS SCORE", 0});
        r.push_back({96, "BONUS:", 1});
        std::snprintf(buf, sizeof buf, "%d  x  10", bonus_);
        r.push_back({96, buf, 2});
        r.push_back({120, "LIFE:", 1});
        std::snprintf(buf, sizeof buf, "%d  x  1000", lives_);
        r.push_back({120, buf, 2});
        r.push_back({144, "SCORE:", 1});
        std::snprintf(buf, sizeof buf, "%06ld", std::min(score_, 999999L));
        r.push_back({144, buf, 2});
        r.push_back({0, reserve_bonus_, 3});
        r.push_back({0, reserve_lives_, 3});
        return r;
    }

    long& score_;
    const int level_;
    int bonus_;   // counting down
    int lives_;   // counting down (a copy: the caller's lives stay)
    const std::vector<formats::Sprite>& charset_;
    const std::vector<formats::Rgb>& pal_;
    const PresentFn& present_;
    const TextScreenHd& hd_;
    const bool hd_on_;
    const std::string reserve_bonus_;
    const std::string reserve_lives_;
    FrameBuffer fb_;                         // classic: native 320x200
    const std::vector<std::uint8_t> black_;  // enhanced: present_hd clears
};

}  // namespace

bool show_score_tally(int& lives, long& score, int display_level, int bonus,
                      const TextPage& page, const TallyAudio& sfx) {
    // Odd display levels award an extra life before the tally.
    if (display_level & 1) ++lives;
    ScoreTally tally(lives, score, display_level, bonus, page);
    // A 4-second hold on each side of the count; SPACE/RETURN skips, edge-
    // triggered.
    if (!tally.show() || !tally.hold(kTallyPauseFrames) || !tally.count() ||
        !tally.show())
        return false;
    // Enhanced completion chime (the reference's extension; the EXE plays
    // nothing here, FUN_270a_01b4 -> silent FUN_1847_065a): once at the final
    // pause, SFX_WAIT_AND_PLAY (ch 9 note 49 cymbal, 400 ms).
    if (sfx.enhanced && sfx.audio != nullptr)
        sfx.audio->play_sfx("SFX_WAIT_AND_PLAY");
    if (!tally.hold(kTallyPauseFrames)) return false;
    // Silent loading screen (all profiles): BONUS.MDI fades here.  A QoL
    // divergence: the EXE issues no fade (FUN_270a_01b4) and the loading
    // screen (FUN_270a_0412) makes no music call, so BONUS loops under
    // "Please Wait".
    if (sfx.audio != nullptr) sfx.audio->fade_out_music();
    return true;
}

}  // namespace olduvai::presentation
