// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Shared SDL window/display helpers.  As in the reference: the window opens at
// the largest integer multiple of the logical canvas that fits the desktop,
// and the renderer scales nearest-neighbour onto a fixed logical size (square
// pixels; fullscreen letterboxes).

#pragma once

#include <SDL.h>
#include <string>

#include "presentation/render/game_render.hpp"

namespace olduvai::presentation {

// Largest integer multiple of (logical_w x logical_h) that fits the
// desktop usable area; never below 1.
int desktop_integer_scale(int logical_w, int logical_h);

// Logical canvas for SDL_RenderSetLogicalSize, by aspect mode:
//   keep    -> (320*scale, 200*scale)  square pixels, black bars (default)
//   4:3     -> (320*scale, 240*scale)  CRT-like vertical stretch
//   stretch -> (0, 0)                  logical scaling off, fills the window
struct LogicalDims { int w; int h; };
// The executable's directory (Contents/Resources in a macOS bundle), trailing
// separators stripped.  "." when SDL cannot answer (an empty prefix would turn
// "<base>/assets" into "/assets").
inline std::string sdl_base_dir() {
    std::string dir;
    if (char* p = SDL_GetBasePath()) {
        dir = p;
        SDL_free(p);
    }
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    return dir.empty() ? std::string(".") : dir;
}

LogicalDims aspect_logical(int scale, const std::string& aspect);

// The session's picture on `ren`: the logical size for `aspect` at `scale`,
// and whole-number scaling in classic (scale 1; classic samples uneven
// nearest columns without it, HD is fine enough).  The flag is inert without
// a logical size ("stretch"), so it follows the scale alone and a live Aspect
// change leaves the renderer as a start at that aspect would.
LogicalDims set_aspect_logical(SDL_Renderer* ren, int scale,
                               const std::string& aspect);

// Default window width for `aspect` at `win_h`.  Only "widescreen" differs: the
// margin comes from the output aspect, so a 16:10 window would show none; take
// the desktop's aspect instead, capped at 2.8 (where the margin caps).
// desktop_ratio <= 0 (unknown) keeps base_w.
int widescreen_default_w(const std::string& aspect, int base_w, int win_h,
                         double desktop_ratio);

struct ScaledWindow {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
};

// Streaming RGBA32 texture, w x h.  Default blend mode; overlays set BLEND
// themselves.
inline bool window_fullscreen(SDL_Window* win) {
    return (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

inline SDL_Texture* create_stream_tex(SDL_Renderer* ren, int w, int h) {
    return SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                             SDL_TEXTUREACCESS_STREAMING, w, h);
}

// Set the project icon on a running window (Linux WMs read it from the window;
// on macOS SDL applies it to the Dock icon).  create_scaled_window calls it.
void set_window_icon(SDL_Window* win);

// The output cleared to black, then `tex` over the whole canvas, or over
// `dst` (the 320 picture pillarboxed on a wide canvas).  The caller presents.
void show_texture(SDL_Renderer* ren, SDL_Texture* tex,
                  const SDL_Rect* dst = nullptr);

// Upload a 320x200 native frame into `tex`, upscaling first when HD is on.
// Deliberately narrow: sites that upload an already-upscaled buffer, stride
// by fb.w or interpose HUD bars are not this.
void upload_native_frame(SDL_Texture* tex, const FrameBuffer& fb, int hd_scale,
                         const std::string& hd_profile);


// A window to create: its title and logical canvas, and how it scales.
struct WindowSpec {
    const char* title = "Olduvai";
    int logical_w = 320;
    int logical_h = 200;
    bool software = false;   // SDL_RENDERER_SOFTWARE (--display-mode cpu)
    bool vsync = false;      // PRESENTVSYNC; off: the engine paces itself
    std::string aspect = "keep";
    // > 0: a forced window size (--window, e.g. 1680x720 for ultrawide);
    // 0: the integer-scaled default.
    int win_w = 0;
    int win_h = 0;
};

// Window at the integer-fit size, accelerated renderer (software fallback),
// nearest-neighbour onto the logical canvas.  The scale-quality hint is set
// before any texture exists (SDL reads it at texture creation).  Always
// resizable.
ScaledWindow create_scaled_window(const WindowSpec& spec);

// Alt+Enter (main or keypad) toggles desktop fullscreen.  Returns true when
// consumed; the caller must then skip it (so Enter does not also advance).
bool handle_fullscreen_toggle(const SDL_Event& ev, SDL_Window* win);

// Drain the queue for a non-gameplay screen (loading card, tally, fade,
// transition, L3 descent): Alt+Enter toggles fullscreen, a window close
// returns false, everything else is discarded, so keys held from the previous
// screen do not leak.  ESC is not handled: its meaning differs per screen.
bool poll_screen_events(SDL_Window* win);

// True while a skip key may read Enter: Alt+Enter belongs to the
// fullscreen toggle, so held-key skip checks must ignore Enter+Alt.
bool enter_skip_allowed();

// Auto-hide the mouse cursor over the window; call once per frame from every
// interactive loop.  Motion shows it, ~1.5 s still hides it.
void cursor_autohide_frame();

// OLDUVAI_AUTO_FULLSCREEN=<frame>: a headless Alt+Enter at that frame.  Unset
// or malformed = never.
void maybe_auto_fullscreen(SDL_Window* win, int frame);

// Drift-free DOS tick: the PIT rate 1193182/65536 = 18.2065 Hz (54.9254 ms).
// A relative SDL_Delay oversleeps 1-10 ms and never pays it back; this keeps
// an absolute deadline on the performance counter: sleep to ~1.5 ms before,
// spin the rest, advance one period.  More than 2 periods behind resyncs
// instead of sprinting.  Wall clock only.
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

// The end of every logic tick, for both drivers:
//   * smooth motion's vsync fill already paced the tick -> re-arm;
//   * --vga-scan classic -> re-present the same frame every vblank until the
//     tick expires (VGA scanning VRAM at 70 Hz);
//   * otherwise -> the drift-free DOS tick's absolute-deadline wait.
// Three sub-1.5 ms presents in a row mean vsync was refused: the scanout
// turns itself off until the next renderer.
class TickPacer {
public:
    void end_tick(SDL_Renderer* ren, SDL_Texture* tex, bool smooth_vsync_ran,
                  bool vga_scan, bool hd);
    // A new renderer (an in-place display rebuild): its vsync is untested.
    void renderer_changed() { vga_scan_ok_ = true; }
    // OLDUVAI_PACE_TRACE: the scanout's presents, and the ticks it filled.
    unsigned long fill_presents() const { return fill_presents_; }
    unsigned long fill_ticks() const { return fill_ticks_; }

private:
    DosTicker ticker_;
    bool vga_scan_ok_ = true;
    unsigned long fill_presents_ = 0;
    unsigned long fill_ticks_ = 0;
};

}  // namespace olduvai::presentation
