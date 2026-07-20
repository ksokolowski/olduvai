// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/screens.hpp"

#include <SDL.h>
#include <cstdio>

#include "enhance/hd_text.hpp"
#include "presentation/audio.hpp"
#include "presentation/hud_render.hpp"

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

// Wait up to `frames` at 18 Hz, returning early on a fresh SPACE/RETURN
// KEYDOWN event (edge-triggered — held keys from gameplay do NOT fire).
// Returns false on QUIT/ESC (abort), true otherwise.
// Mirrors the reference's _wait_skippable (FUN_1847_0670 fire-key polling):
// the EXE's polled loop is calibrated-delay; we translate to event edges so
// a key held when the tally begins does not blow through the pause instantly.
bool tally_pause(const PresentFn& present, const FrameBuffer& fb,
                 int frames) {
    for (int f = 0; f < frames; ++f) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) return false;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
                // Alt+Enter belongs to the fullscreen chord, not the skip
                // (enter_skip_allowed convention); requeue so the present
                // path's poll sees and handles the toggle.
                if ((ev.key.keysym.sym == SDLK_RETURN ||
                     ev.key.keysym.sym == SDLK_KP_ENTER) &&
                    (ev.key.keysym.mod & KMOD_ALT) != 0) {
                    SDL_PushEvent(&ev);
                    break;
                }
                if (ev.key.keysym.sym == SDLK_SPACE ||
                    ev.key.keysym.sym == SDLK_RETURN) {
                    return true;   // fresh key-down → skip pause
                }
            }
        }
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

void apply_fade(FrameBuffer& dst, const FrameBuffer& src, double t) {
    const int mul = static_cast<int>((1.0 - t) * 256.0);
    for (std::size_t i = 0; i < src.px.size(); i += 4) {
        dst.px[i] = static_cast<std::uint8_t>(src.px[i] * mul >> 8);
        dst.px[i + 1] = static_cast<std::uint8_t>(src.px[i + 1] * mul >> 8);
        dst.px[i + 2] = static_cast<std::uint8_t>(src.px[i + 2] * mul >> 8);
        dst.px[i + 3] = 255;
    }
}

// ── show_loading_screen ──────────────────────────────────────────────────────

bool show_loading_screen(const FrameBuffer* from, int display_level,
                         const std::vector<formats::Sprite>& charset,
                         const std::vector<formats::Rgb>& pal,
                         const PresentFn& present,
                         const LoadingHd& hd) {
    char line2[40];
    std::snprintf(line2, sizeof line2, "while loading Level %d",
                  display_level);

    // --enhance hd-text: render the two rows through the cartoon vector font on
    // an HD-upscaled black buffer, presented via hd.present_hd — mirrors the
    // TallyHd path in show_score_tally.  Null hd_text →
    // classic bitmap path below (byte-identical).
    const bool hd_on = hd.hd_text != nullptr && hd.hd_text->ok() &&
                       hd.present_hd && hd.upscale;

    if (hd_on) {
        const int hw = 320 * hd.scale;
        const int hh = 200 * hd.scale;
        // The two vector rows at the EXE baselines (0x60/0x70); drawn at
        // OUTPUT resolution by present_hd as a crisp overlay.  The scene
        // buffer carries no text (black, or the fading "from" frame).
        const std::vector<HdTextRow> rows = {{0x60, "Please Wait"},
                                             {0x70, line2}};
        const std::vector<HdTextRow> no_rows;
        FrameBuffer black;   // native black (alpha=255), no bitmap text
        for (std::size_t i = 3; i < black.px.size(); i += 4) black.px[i] = 255;
        const std::vector<std::uint8_t> loading_hd = hd.upscale(black.px);
        // Pre-upscale the "from" frame once (HD pixels already at HD size when
        // the caller's `from` is HD — but show_loading_screen takes a native
        // FrameBuffer, so upscale it the same way the tally upscales its base).
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
    draw_centered(loading, charset, pal, 0x60, "Please Wait");
    draw_centered(loading, charset, pal, 0x70, line2);

    FrameBuffer work;
    if (from != nullptr) {              // fade current → black
        for (int f = 0; f <= kFadeFrames; ++f) {
            apply_fade(work, *from, static_cast<double>(f) / kFadeFrames);
            if (!present(work)) return false;
        }
    }
    for (int f = kFadeFrames; f >= 0; --f) {   // fade in the loading text
        apply_fade(work, loading, static_cast<double>(f) / kFadeFrames);
        if (!present(work)) return false;
    }
    for (int f = 0; f < 9; ++f) {              // hold ~0.5 s
        if (!present(loading)) return false;
    }
    for (int f = 0; f <= kFadeFrames; ++f) {   // fade out to black
        apply_fade(work, loading, static_cast<double>(f) / kFadeFrames);
        if (!present(work)) return false;
    }
    return true;
}

// ── show_pc1_screen ──────────────────────────────────────────────────────────

bool show_pc1_screen(const formats::Pc1Image& img, int hold_frames,
                     const PresentFn& present, const SkipFn& skip,
                     bool fade_in, bool fade_out) {
    FrameBuffer fb;
    const std::size_t n = std::min<std::size_t>(img.pixels.size(), 320 * 200);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t idx = img.pixels[i];
        const auto c = (idx < img.palette.size()) ? img.palette[idx]
                                                  : formats::Rgb{};
        fb.px[i * 4] = c.r;
        fb.px[i * 4 + 1] = c.g;
        fb.px[i * 4 + 2] = c.b;
        fb.px[i * 4 + 3] = 255;
    }
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
    if (fade_out) {
        for (int f = 0; f <= kFadeFrames; ++f) {
            apply_fade(work, fb, static_cast<double>(f) / kFadeFrames);
            if (!present(work)) return false;
        }
    }
    return true;
}

// ── show_score_tally (boss overload) ────────────────────────────────────────
//
// Takes lives/score by reference directly — used for boss levels where the
// caller holds a BossPlayerState (no SystemsState wrapper).
// EXE: Level_EndScreen(N,500) at 23cf:0fc9 / 24cc:0818 / 254f:0620.

bool show_score_tally(int& lives, long& score, int display_level,
                      int bonus, const std::vector<formats::Sprite>& charset,
                      const std::vector<formats::Rgb>& pal,
                      const PresentFn& present, const SkipFn& /*skip*/,
                      const TallyHd& hd, const TallyAudio& sfx) {
    // Odd display levels award an extra life before the tally.
    if (display_level & 1) ++lives;

    int bonus_remaining = bonus;
    int lives_remaining = lives;

    // --enhance hd-text: route every text row through the cartoon vector font
    // at HD resolution.  The bitmap base is left black (text suppressed) and
    // the vector glyphs are drawn on the upscaled buffer, exactly like
    // the reference text layer (which suppresses the bitmap draw and flushes vector
    // text at display res over the upscaled scene).  Null hd_text → classic.
    const bool hd_on = hd.hd_text != nullptr && hd.hd_text->ok() &&
                       hd.present_hd && hd.upscale;

    FrameBuffer fb;        // native 320×200 base (black bg + alpha)
    auto fill_base = [&]() {
        std::fill(fb.px.begin(), fb.px.end(), 0);
        for (std::size_t i = 3; i < fb.px.size(); i += 4) fb.px[i] = 255;
    };

    // Build the classic (bitmap) counting-row strings.  Centred, EXE-faithful
    // "%6d"-padded layout — used ONLY by render_bitmap() below.  The HD path
    // uses build_rows() (fixed-anchor label/value rows) instead, so the cartoon
    // font does not slide as the digit widths change.
    auto rows = [&](char b1[40], char b2[40], char b3[40]) {
        std::snprintf(b1, 40, "BONUS : %6d  x  10  ", bonus_remaining);
        std::snprintf(b2, 40, "LIFE  : %6d  x  1000", lives_remaining);
        std::snprintf(b3, 40, "SCORE : %06ld", std::min(score, 999999L));
    };

    auto render_bitmap = [&]() {
        fill_base();
        char buf[40], b2[40], b3[40];
        std::snprintf(buf, sizeof buf, "LEVEL %d", display_level);
        draw_centered(fb, charset, pal, 32, buf);
        draw_centered(fb, charset, pal, 48, "COMPLETED!");
        draw_centered(fb, charset, pal, 72, "BONUS SCORE");
        rows(buf, b2, b3);
        draw_centered(fb, charset, pal, 96, buf);
        draw_centered(fb, charset, pal, 120, b2);
        draw_centered(fb, charset, pal, 144, b3);
    };

    // Build the HD tally rows at their EXE baselines.  The three title rows are
    // centred (align 0); the three counting rows are each split into a fixed
    // right-aligned label (align 1) + a left-aligned value (align 2) so the
    // proportional cartoon font does not re-centre — and slide — as the bonus
    // counts down and the score counts up (the reference's _record_tally_rows layout).
    // draw_tally_rows_overlay derives the colon/value columns from fixed
    // reference strings, so the columns are stable every frame.  Values use RAW
    // numbers (no %6d padding) like the reference: "<bonus>  x  10",
    // "<lives>  x  1000", "<score:06>".  render_bitmap() keeps the EXE-centred
    // "%6d" strings (classic path unchanged).
    auto build_rows = [&]() -> std::vector<HdTextRow> {
        char buf[40];
        std::vector<HdTextRow> r;
        std::snprintf(buf, sizeof buf, "LEVEL %d", display_level);
        r.push_back({32, buf, 0});
        r.push_back({48, "COMPLETED!", 0});
        r.push_back({72, "BONUS SCORE", 0});

        r.push_back({96, "BONUS:", 1});
        std::snprintf(buf, sizeof buf, "%d  x  10", bonus_remaining);
        r.push_back({96, buf, 2});

        r.push_back({120, "LIFE:", 1});
        std::snprintf(buf, sizeof buf, "%d  x  1000", lives_remaining);
        r.push_back({120, buf, 2});

        r.push_back({144, "SCORE:", 1});
        std::snprintf(buf, sizeof buf, "%06ld", std::min(score, 999999L));
        r.push_back({144, buf, 2});
        return r;
    };

    // Present one tally frame for the current state.  HD: upscale the black
    // base (no text) and pass the rows for the output-res overlay.
    auto present_state = [&]() -> bool {
        if (!hd_on) {
            render_bitmap();
            return present(fb);
        }
        fill_base();                               // black base, no bitmap text
        std::vector<std::uint8_t> hd_px = hd.upscale(fb.px);
        const int hw = 320 * hd.scale;
        const int hh = 200 * hd.scale;
        return hd.present_hd(hd_px, hw, hh, build_rows());
    };

    // Wait up to `frames`, presenting the current (static) state each frame,
    // edge-triggered SPACE/RETURN skip.  Classic uses tally_pause on the
    // bitmap fb; HD re-presents the composed HD scene + overlay each frame.
    auto pause = [&](int frames) -> bool {
        if (!hd_on) return tally_pause(present, fb, frames);
        fill_base();
        std::vector<std::uint8_t> hd_px = hd.upscale(fb.px);
        const int hw = 320 * hd.scale;
        const int hh = 200 * hd.scale;
        const std::vector<HdTextRow> row_list = build_rows();
        for (int f = 0; f < frames; ++f) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) return false;
                if (ev.type == SDL_KEYDOWN) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
                    if ((ev.key.keysym.sym == SDLK_RETURN ||
                         ev.key.keysym.sym == SDLK_KP_ENTER) &&
                        (ev.key.keysym.mod & KMOD_ALT) != 0) {
                        SDL_PushEvent(&ev);
                        break;
                    }
                    if (ev.key.keysym.sym == SDLK_SPACE ||
                        ev.key.keysym.sym == SDLK_RETURN)
                        return true;
                }
            }
            if (!hd.present_hd(hd_px, hw, hh, row_list)) return false;
        }
        return true;
    };

    if (!present_state()) return false;
    // First 4-second pause: edge-triggered SPACE/RETURN skip (not held state).
    if (!pause(kTallyPauseFrames)) return false;

    // Enhanced #06 — tally-roll skip (owner-ruled DEFAULT, all profiles,
    // 2026-07-05): SPACE/RETURN during either roll fast-forwards the SAME
    // per-step arithmetic (identical final score), then falls into the
    // post-tally pause.  The EXE cannot skip the rolls (FUN_270a_01b4
    // 0x0303-0x03bd are vsync-only loops, no input poll — catalog #06).
    // QUIT/ESC aborts mid-roll (consistent with the pause handling above).
    auto roll_keys = [&]() -> int {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) return -1;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) return -1;
                if ((ev.key.keysym.sym == SDLK_RETURN ||
                     ev.key.keysym.sym == SDLK_KP_ENTER) &&
                    (ev.key.keysym.mod & KMOD_ALT) != 0) {
                    SDL_PushEvent(&ev);
                    break;
                }
                if (ev.key.keysym.sym == SDLK_SPACE ||
                    ev.key.keysym.sym == SDLK_RETURN)
                    return 1;
            }
        }
        return 0;
    };
    auto fast_forward = [&]() {
        while (bonus_remaining > 0) step_tally_bonus(bonus_remaining, score);
        while (lives_remaining > 0) step_tally_lives(lives_remaining, score);
    };
    // Bonus countdown: −2 per frame, +20 score.
    while (bonus_remaining > 0) {
        step_tally_bonus(bonus_remaining, score);
        if (!present_state()) return false;
        const int k = roll_keys();
        if (k < 0) return false;
        if (k > 0) { fast_forward(); break; }
    }
    // Lives countdown: −1 per frame, +1000 score.
    while (lives_remaining > 0) {
        step_tally_lives(lives_remaining, score);
        if (!present_state()) return false;
        const int k = roll_keys();
        if (k < 0) return false;
        if (k > 0) { fast_forward(); break; }
    }
    if (!present_state()) return false;
    // Enhanced completion chime — engine extension, NOT EXE-derived.  Fires
    // once at the final pause (after both countdowns), over the BONUS music,
    // exactly where the reference plays it
    // (`if state.cinematic_cue: audio.play_sfx_event("SFX_WAIT_AND_PLAY")`,
    // gated by cinematic-cue which --enhanced enables).  SFX_WAIT_AND_PLAY is
    // a synth note (catalog: ch 9 / note 49 crash-cymbal / 400 ms) routed
    // through play_sfx; the default sb-dac path has no sample so it is silent
    // there too.  The EXE plays NO SFX here (FUN_270a_01b4 → silent
    // FUN_1847_065a wait), so the non-enhanced / null-audio path stays silent.
    if (sfx.enhanced && sfx.audio != nullptr) {
        sfx.audio->play_sfx("SFX_WAIT_AND_PLAY");
    }
    // Final 4-second pause: edge-triggered.
    if (!pause(kTallyPauseFrames)) return false;
    // Enhanced #13 — silent loading screen (owner-ruled DEFAULT, all
    // profiles, 2026-07-05): fade BONUS.MDI out here so the "Please Wait"
    // screen plays in silence.  EXE truth is the OPPOSITE — the tally tail
    // (FUN_270a_01b4) issues no MDI_FadeStop, the loading screen
    // (FUN_270a_0412) makes no music call, and the level main swaps tracks
    // only after; BONUS audibly loops over the loading text (Finding
    // loading_screen_not_silent_bonus_bleeds_through.md, conf A — the
    // FIX-H revert preserved that until this ruling).  Documented QoL
    // divergence, same ruling class as the tally fonts.  Audio-only.
    if (sfx.audio != nullptr) sfx.audio->fade_out_music();
    return true;
}

// ── show_score_tally (SystemsState overload) ─────────────────────────────────

bool show_score_tally(systems::SystemsState& state, int display_level,
                      int bonus, const std::vector<formats::Sprite>& charset,
                      const std::vector<formats::Rgb>& pal,
                      const PresentFn& present, const SkipFn& skip,
                      const TallyHd& hd, const TallyAudio& sfx) {
    // Delegate to the lives/score-reference overload.
    return show_score_tally(state.player.lives, state.score, display_level,
                            bonus, charset, pal, present, skip, hd, sfx);
}

}  // namespace olduvai::presentation
