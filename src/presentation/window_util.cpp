// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/window_util.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG   // the embedded icon is the only image this decodes
// Vendored single-header: silence its own warnings under -Werror.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "stb_image.h"
#pragma GCC diagnostic pop

// Generated TU (cmake/embed_binary.cmake from assets/icon/icon_src.png): the
// bone project logo, so every window carries the icon without a file lookup.
extern const unsigned char embedded_icon_png[];
extern const unsigned long embedded_icon_png_len;

namespace olduvai::presentation {

LogicalDims aspect_logical(int scale, const std::string& aspect) {
    if (aspect == "stretch") return {0, 0};
    if (aspect == "4:3")     return {320 * scale, 240 * scale};
    // "widescreen" without an active wide framebuffer (classic mode, or no
    // coherent neighbor) falls back to keep — a clean pillarbox.  The active
    // widescreen present path computes its own logical size from the wide
    // framebuffer (320 + 2*margin) in run_platform_level.
    return {320 * scale, 200 * scale};  // keep (default) / widescreen fallback
}

int desktop_integer_scale(int logical_w, int logical_h) {
    SDL_Rect usable{0, 0, 0, 0};
    if (SDL_GetDisplayUsableBounds(0, &usable) != 0 || usable.w <= 0 ||
        usable.h <= 0) {
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
            usable.w = dm.w;
            usable.h = dm.h;
        } else {
            return 1;
        }
    }
    const int kw = usable.w / logical_w;
    const int kh = usable.h / logical_h;
    const int k = kw < kh ? kw : kh;
    return k < 1 ? 1 : k;
}

ScaledWindow create_scaled_window(const char* title, int logical_w,
                                  int logical_h, bool software, bool vsync,
                                  const std::string& aspect, int win_w,
                                  int win_h, bool integer_scale) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    const int k = desktop_integer_scale(logical_w, logical_h);
    // Explicit --window WxH override (e.g. 1680x720 ≈ 21:9 to simulate an
    // ultrawide viewport on a non-ultrawide panel for widescreen testing);
    // otherwise the integer-scaled default.  RESIZABLE so the aspect can also be
    // dragged at runtime (rebuild_ws_if_resized recomputes the margin).
    const int win_px_w = win_w > 0 ? win_w : logical_w * k;
    const int win_px_h = win_h > 0 ? win_h : logical_h * k;
    ScaledWindow sw;
    // ALLOW_HIGHDPI so SDL_GetRendererOutputSize reports TRUE physical pixels
    // on HiDPI/Retina displays (e.g. 2560×1600 backing a 1280×800-point
    // window).  The scene texture still nearest-scales onto the logical canvas
    // (SDL_RenderSetLogicalSize below), but the output-resolution vector-text
    // overlay (presentation/text_overlay) draws at the physical pixel count, so
    // HUD/label glyphs are crisp instead of being upscaled by the OS.
    // Present as "Olduvai" everywhere the platform names the app rather than
    // the window: X11 WM_CLASS / Wayland app_id (read at window creation) and
    // the PulseAudio stream name.  macOS naming comes from the Info.plist
    // (bundle) / embedded __info_plist section (bare CLI binary) instead.
#ifdef SDL_HINT_APP_NAME
    SDL_SetHint(SDL_HINT_APP_NAME, "Olduvai");
#endif
    sw.win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, win_px_w, win_px_h,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI |
                                  SDL_WINDOW_RESIZABLE);
    if (sw.win == nullptr) return sw;
    set_window_icon(sw.win);
    // --display-mode: gpu = SDL_RENDERER_ACCELERATED (GPU scaling),
    // cpu = SDL_RENDERER_SOFTWARE (CPU window scaling).  --vsync adds
    // PRESENTVSYNC on top (off by default; ignored by drivers that refuse).
    Uint32 ren_flags =
        software ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
    if (vsync) ren_flags |= SDL_RENDERER_PRESENTVSYNC;
    sw.ren = SDL_CreateRenderer(sw.win, -1, ren_flags);
    if (sw.ren == nullptr) {
        // Drop vsync first (a refused PRESENTVSYNC can fail the whole
        // create), then fall back to any driver.
        sw.ren = SDL_CreateRenderer(
            sw.win, -1, software ? SDL_RENDERER_SOFTWARE : 0);
    }
    if (sw.ren == nullptr) sw.ren = SDL_CreateRenderer(sw.win, -1, 0);
    if (sw.ren != nullptr) {
        const LogicalDims ld = aspect_logical(logical_w / 320, aspect);
        SDL_RenderSetLogicalSize(sw.ren, ld.w, ld.h);
        // Classic pixel integrity (roadmap OL-A1): whole-number scaling only
        // — fullscreen at a non-multiple resolution otherwise nearest-samples
        // at e.g. 1.5x and produces uneven pixel columns.  Letterboxes a bit
        // more instead.  No-op for "stretch" (logical size disabled).
        if (integer_scale && ld.w > 0)
            SDL_RenderSetIntegerScale(sw.ren, SDL_TRUE);
    }
    return sw;
}

bool handle_fullscreen_toggle(const SDL_Event& ev, SDL_Window* win) {
    if (win == nullptr || ev.type != SDL_KEYDOWN) return false;
    if (ev.key.keysym.sym != SDLK_RETURN &&
        ev.key.keysym.sym != SDLK_KP_ENTER) {
        return false;
    }
    if ((ev.key.keysym.mod & KMOD_ALT) == 0) return false;
    const Uint32 flags = SDL_GetWindowFlags(win);
    const bool fullscreen =
        (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(win, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    return true;
}

bool enter_skip_allowed() {
    return (SDL_GetModState() & KMOD_ALT) == 0;
}

DosTicker::DosTicker() {
    freq_ = SDL_GetPerformanceFrequency();
    period_counts_ = static_cast<double>(freq_) * 65536.0 / 1193182.0;
    arm();
}

void DosTicker::arm() {
    next_ = static_cast<double>(SDL_GetPerformanceCounter()) + period_counts_;
}

void DosTicker::wait_next() {
    const double now0 = static_cast<double>(SDL_GetPerformanceCounter());
    if (now0 > next_ + 2.0 * period_counts_) {   // fell far behind: resync
        next_ = now0 + period_counts_;
        return;
    }
    // Coarse sleep to ~1.5 ms before the deadline (SDL_Delay oversleeps),
    // then spin the rest for sub-ms landing.
    const double margin = static_cast<double>(freq_) * 0.0015;
    double now = now0;
    if (now < next_ - margin) {
        const double remain_ms = (next_ - margin - now) * 1000.0 /
                                 static_cast<double>(freq_);
        if (remain_ms >= 1.0) SDL_Delay(static_cast<Uint32>(remain_ms));
    }
    while (static_cast<double>(SDL_GetPerformanceCounter()) < next_) {
        // spin (~<=1.5 ms per tick at 18 Hz = <3% of one core)
    }
    next_ += period_counts_;
}

bool DosTicker::pending() const {
    return static_cast<double>(SDL_GetPerformanceCounter()) < next_;
}

void DosTicker::advance() {
    const double now = static_cast<double>(SDL_GetPerformanceCounter());
    if (now > next_ + 2.0 * period_counts_) next_ = now + period_counts_;
    else next_ += period_counts_;
}

void cursor_autohide_frame() {
    static int last_x = -1, last_y = -1;
    static Uint32 last_motion_ms = 0;
    static bool hidden = false;
    constexpr Uint32 kIdleMs = 1500;
    int x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    const Uint32 now = SDL_GetTicks();
    if (x != last_x || y != last_y) {
        last_x = x;
        last_y = y;
        last_motion_ms = now;
        if (hidden) {
            SDL_ShowCursor(SDL_ENABLE);
            hidden = false;
        }
    } else if (!hidden && now - last_motion_ms > kIdleMs) {
        SDL_ShowCursor(SDL_DISABLE);   // SDL scope: only over OUR window
        hidden = true;
    }
}

void set_window_icon(SDL_Window* win) {
    // Decode the embedded logo and hand it to SDL.  Launchers get the icon
    // from .desktop/.ico/.icns files, but Linux window managers take the
    // RUNNING window's icon (alt-tab, taskbar) from the window itself.
    // On macOS SDL applies it to the Dock icon of the running process
    // (observed 2026-07-11 on SDL 2.30); Finder still uses the bundle .icns.
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(
        embedded_icon_png, static_cast<int>(embedded_icon_png_len),
        &w, &h, &comp, 4);
    if (px == nullptr) return;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
        px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (s != nullptr) {
        SDL_SetWindowIcon(win, s);   // SDL copies the pixels
        SDL_FreeSurface(s);
    }
    stbi_image_free(px);
}

}  // namespace olduvai::presentation
