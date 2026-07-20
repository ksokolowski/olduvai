// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Shared SDL window/display helpers for every user-facing surface
// (gameplay, intro, ending, boss arenas).
//
// Sizing matches the reference engine's default gpu display path
// (the reference run_game: logical game surface + pygame.SCALED): the window
// opens at the LARGEST INTEGER MULTIPLE of the logical canvas that fits
// the desktop, and the renderer scales nearest-neighbour onto a fixed
// logical size — so pixels stay square and crisp at any window size,
// and fullscreen letterboxes instead of distorting.

#pragma once

#include <SDL.h>
#include <string>

namespace olduvai::presentation {

// Largest integer multiple of (logical_w × logical_h) that fits the
// desktop usable area; never below 1.
int desktop_integer_scale(int logical_w, int logical_h);

// The logical canvas dimensions fed to SDL_RenderSetLogicalSize for a given
// render scale and aspect mode:
//   keep    → (320*scale, 200*scale)  — square pixels, black bars (default)
//   4:3     → (320*scale, 240*scale)  — CRT-like vertical stretch
//   stretch → (0, 0)                  — disables logical scaling, fills window
struct LogicalDims { int w; int h; };
LogicalDims aspect_logical(int scale, const std::string& aspect);

struct ScaledWindow {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
};

// Set the project icon (the embedded bone logo) on a window.  Launchers get
// the icon from .desktop/.ico/.icns; this covers the RUNNING window — Linux
// window managers (alt-tab, taskbar) take it from the window itself, and on
// macOS SDL applies it to the running process's Dock icon.
// create_scaled_window calls it; standalone SDL windows (viewer) call it
// directly.
void set_window_icon(SDL_Window* win);

// Window at the integer-fit size + accelerated renderer (software
// fallback) with nearest-neighbour scaling onto the logical canvas.
// The scale-quality hint is set here, BEFORE any texture exists —
// SDL reads it at texture-creation time.
//
// `software` forces SDL_RENDERER_SOFTWARE (the --display-mode cpu path:
// CPU window scaling instead of the GPU; the reference run_game's
// `gpu`=hardware / `cpu`=software split).  `vsync` adds
// SDL_RENDERER_PRESENTVSYNC (off by default — the engine paces via its
// own clock, and the driver silently ignores the flag if unsupported).
// win_w/win_h (>0) force an explicit window pixel size (e.g. 1680x720 ≈ 21:9 to
// simulate an ultrawide viewport for widescreen testing); 0 = integer-scaled
// default.  The window is RESIZABLE either way.
ScaledWindow create_scaled_window(const char* title, int logical_w,
                                  int logical_h, bool software = false,
                                  bool vsync = false,
                                  const std::string& aspect = "keep",
                                  int win_w = 0, int win_h = 0,
                                  // integer_scale: force whole-number scaling
                                  // of the logical canvas (classic mode —
                                  // non-integer fullscreen ratios give uneven
                                  // nearest-sampled pixel columns at 320x200;
                                  // HD output is 4x finer so it stays free).
                                  bool integer_scale = false);

// Alt+Enter (main or keypad Enter) ↔ desktop-fullscreen toggle, as in
// the reference (_is_fullscreen_toggle_event/_toggle_fullscreen).
// Returns true when the event was consumed — callers must then skip
// their own handling so Enter-driven screens don't also advance
// (the reference swallows the event for the same reason).
bool handle_fullscreen_toggle(const SDL_Event& ev, SDL_Window* win);

// True while a skip key may read Enter: Alt+Enter belongs to the
// fullscreen toggle, so held-key skip checks must ignore Enter+Alt.
bool enter_skip_allowed();

// Auto-hide the OS mouse cursor over the game window: call once per frame
// from every interactive loop.  Polls the cursor position (no event plumbing)
// — any motion shows the cursor and stamps a timer; ~1.5 s of stillness hides
// it again.  The game is keyboard-only, so a parked arrow over the playfield
// (especially fullscreen) is pure noise.
void cursor_autohide_frame();

// Drift-free DOS tick scheduler.  The EXE runs off the PIT at
// 1193182/65536 = 18.2065 Hz (54.9254 ms); DOSBox emulates that
// metronomically, which is why it FEELS smoother than a naive
// `SDL_Delay(55 - spent)` loop: SDL_Delay oversleeps by scheduler slop
// (1-10 ms on macOS) and a relative delay never pays the overshoot back,
// so steps land unevenly (~15-17 Hz with jitter).  This ticker keeps an
// ABSOLUTE deadline on the SDL performance counter: coarse-sleep to
// ~1.5 ms before the deadline, spin the remainder, advance by exactly one
// period.  Falling behind by >2 periods (level load, transition player)
// resyncs instead of sprinting.  Wall-clock only — no logic/RNG contact.
class DosTicker {
public:
    DosTicker();
    void arm();         // resync: next deadline = now + one period
    void wait_next();   // block until the deadline, then advance it
    bool pending() const;   // still before the deadline?
    void advance();         // deadline += one period (resyncs if far behind)
private:
    double period_counts_ = 0.0;
    double next_ = 0.0;
    unsigned long long freq_ = 0;
};

}  // namespace olduvai::presentation
