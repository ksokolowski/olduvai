// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/window_util.hpp"

#include "presentation/env_num.hpp"   // env_int

#include <algorithm>
#include <cstdio>
#include <string>
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG   // the embedded icon is the only image this decodes
// Vendored single-header: silence its own warnings under -Werror.
#if defined(__GNUC__) || defined(__clang__)   // MSVC: C4068 unknown pragma
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "stb_image.h"
#if defined(__GNUC__) || defined(__clang__)   // MSVC: C4068 unknown pragma
#pragma GCC diagnostic pop
#endif

// Generated TU (cmake/embed_binary.cmake from assets/icon/icon_src.png): the
// bone project logo, so every window carries the icon without a file lookup.
extern const unsigned char embedded_icon_png[];
extern const unsigned long embedded_icon_png_len;

#include "enhance/upscale.hpp"

namespace olduvai::presentation {

void TickPacer::end_tick(SDL_Renderer* ren, SDL_Texture* tex,
                         bool smooth_vsync_ran, bool vga_scan, bool hd) {
    if (smooth_vsync_ran) {
        // The vsync render-fill already paced this tick to (about) frame_ms
        // via the panel; keep the ticker in phase without an extra sleep.
        ticker_.arm();
        return;
    }
    if (vga_scan && !hd && vga_scan_ok_) {
        // Hold-frame scanout: re-present the same frame every vblank until the
        // tick expires.  A driver that refused vsync returns instantly; three
        // presents under 1.5 ms in a row switch to timer pacing for the level.
        int fast_presents = 0;
        while (ticker_.pending()) {
            const Uint64 p0 = SDL_GetPerformanceCounter();
            show_texture(ren, tex);
            // Not present_output: this frame was dumped when first shown.
            SDL_RenderPresent(ren);
            ++fill_presents_;
            const double ms = (SDL_GetPerformanceCounter() - p0) * 1000.0 /
                              static_cast<double>(SDL_GetPerformanceFrequency());
            if (ms < 1.5) {
                if (++fast_presents >= 3) {
                    vga_scan_ok_ = false;
                    std::fprintf(stderr,
                                 "pacing: vsync appears refused — "
                                 "vga-scan off, timer pacing\n");
                    break;
                }
            } else {
                fast_presents = 0;
            }
        }
        ++fill_ticks_;
        if (ticker_.pending()) ticker_.wait_next();
        else ticker_.advance();
        return;
    }
    // Absolute-deadline wait at the PIT rate (a relative SDL_Delay oversleeps
    // every frame).
    ticker_.wait_next();
}

void show_texture(SDL_Renderer* ren, SDL_Texture* tex, const SDL_Rect* dst) {
    // Pinned: the renderer is shared, and a stale draw colour would tint the
    // clear (the bars of a pillarbox).
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (tex != nullptr) SDL_RenderCopy(ren, tex, nullptr, dst);
}

void upload_native_frame(SDL_Texture* tex, const FrameBuffer& fb, int hd_scale,
                         const std::string& hd_profile) {
    if (hd_scale > 1) {
        const auto up =
            enhance::upscale_rgba(fb.px, 320, 200, hd_scale, hd_profile);
        SDL_UpdateTexture(tex, nullptr, up.data(), 320 * hd_scale * 4);
    } else {
        SDL_UpdateTexture(tex, nullptr, fb.px.data(), 320 * 4);
    }
}

LogicalDims aspect_logical(int scale, const std::string& aspect) {
    if (aspect == "stretch") return {0, 0};
    if (aspect == "4:3")     return {320 * scale, 240 * scale};
    // "widescreen" without a wide framebuffer (classic, no neighbour) falls
    // back to keep; the wide present sets its own logical size (320 +
    // 2*margin).
    return {320 * scale, 200 * scale};  // keep (default) / widescreen fallback
}

LogicalDims set_aspect_logical(SDL_Renderer* ren, int scale,
                               const std::string& aspect) {
    const LogicalDims ld = aspect_logical(scale, aspect);
    SDL_RenderSetLogicalSize(ren, ld.w, ld.h);
    SDL_RenderSetIntegerScale(ren, scale == 1 ? SDL_TRUE : SDL_FALSE);
    return ld;
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

int widescreen_default_w(const std::string& aspect, int base_w, int win_h,
                         double desktop_ratio) {
    if (aspect != "widescreen" || win_h <= 0 || desktop_ratio <= 0.0)
        return base_w;
    // 2.8 is where boss_ws_margin's 120-column cap bites: past it there is no
    // more arena to show, so a wider window would only letterbox.
    if (desktop_ratio > 2.8) desktop_ratio = 2.8;
    const int w = static_cast<int>(win_h * desktop_ratio + 0.5);
    return w > base_w ? w : base_w;   // never NARROWER than the DOS window
}

namespace {

// --window WxH (e.g. 1680x720 to test ultrawide), else the integer-scaled
// default.  --aspect widescreen without --window: a window of the desktop's
// aspect, never wider than the desktop nor narrower than the default.  Only
// that case is clamped: clamping the integer-scaled default shrank the 1280
// default to 1024 on the headless driver.
int initial_window_width(const WindowSpec& spec, int default_w, int win_h) {
    if (spec.win_w > 0) return spec.win_w;
    if (spec.aspect != "widescreen") return default_w;
    SDL_Rect usable{0, 0, 0, 0};
    double ratio = 0.0;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.h > 0)
        ratio = static_cast<double>(usable.w) / usable.h;
    const int wide = widescreen_default_w(spec.aspect, default_w, win_h, ratio);
    if (usable.w > 0 && wide > usable.w)
        return usable.w > default_w ? usable.w : default_w;
    return wide;
}

// --display-mode gpu = ACCELERATED, cpu = SOFTWARE; --vsync adds PRESENTVSYNC.
// A refused create drops vsync first (a refused PRESENTVSYNC can fail the
// whole create), then takes any driver.
SDL_Renderer* create_renderer(SDL_Window* win, const WindowSpec& spec) {
    const Uint32 base =
        spec.software ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED;
    SDL_Renderer* ren = SDL_CreateRenderer(
        win, -1, spec.vsync ? base | SDL_RENDERER_PRESENTVSYNC : base);
    if (ren == nullptr)
        ren = SDL_CreateRenderer(win, -1,
                                 spec.software ? SDL_RENDERER_SOFTWARE : 0);
    if (ren == nullptr) ren = SDL_CreateRenderer(win, -1, 0);
    return ren;
}

// OLDUVAI_FRAME_STATS: name the backend and whether it takes RGBA32 natively;
// if not, SDL converts every full-screen upload on the CPU.
void log_renderer_info(SDL_Renderer* ren) {
    SDL_RendererInfo ri;
    if (SDL_GetRendererInfo(ren, &ri) != 0) return;
    const bool native = std::any_of(
        ri.texture_formats, ri.texture_formats + ri.num_texture_formats,
        [](Uint32 f) { return f == SDL_PIXELFORMAT_RGBA32; });
    std::fprintf(stderr,
                 "renderer: %s accel=%d vsync=%d max_tex=%dx%d "
                 "rgba32_native=%s\n",
                 ri.name != nullptr ? ri.name : "?",
                 (ri.flags & SDL_RENDERER_ACCELERATED) ? 1 : 0,
                 (ri.flags & SDL_RENDERER_PRESENTVSYNC) ? 1 : 0,
                 ri.max_texture_width, ri.max_texture_height,
                 native ? "yes" : "NO (SDL converts every upload)");
}

}  // namespace

// Resizable: the margin recomputes on resize.
ScaledWindow create_scaled_window(const WindowSpec& spec) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    const int k = desktop_integer_scale(spec.logical_w, spec.logical_h);
    const int win_px_h = spec.win_h > 0 ? spec.win_h : spec.logical_h * k;
    const int win_px_w =
        initial_window_width(spec, spec.logical_w * k, win_px_h);
    ScaledWindow sw;
    // ALLOW_HIGHDPI so SDL_GetRendererOutputSize reports physical pixels on
    // Retina, and the text overlay draws at that resolution.
    // App name "Olduvai" for X11 WM_CLASS / Wayland app_id (read at window
    // creation) and the PulseAudio stream; macOS takes it from the Info.plist.
#ifdef SDL_HINT_APP_NAME
    SDL_SetHint(SDL_HINT_APP_NAME, "Olduvai");
#endif
    sw.win = SDL_CreateWindow(spec.title, SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, win_px_w, win_px_h,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI |
                                  SDL_WINDOW_RESIZABLE);
    if (sw.win == nullptr) return sw;
    set_window_icon(sw.win);
    sw.ren = create_renderer(sw.win, spec);
    if (sw.ren == nullptr) return sw;
    if (std::getenv("OLDUVAI_FRAME_STATS") != nullptr) log_renderer_info(sw.ren);
    set_aspect_logical(sw.ren, spec.logical_w / 320, spec.aspect);
    return sw;
}

bool init_sdl_video(Uint32 extra_flags) {
    const Uint32 flags = SDL_INIT_VIDEO | extra_flags;
    if (SDL_InitSubSystem(flags) != 0) {
        const std::string first = SDL_GetError();
        const char* asked = SDL_getenv("SDL_VIDEODRIVER");
        std::fprintf(stderr, "video: driver %s did not start (%s); trying each\n",
                     asked != nullptr ? asked : "(default)", first.c_str());
        bool up = false;
        // SDL reads SDL_VIDEODRIVER at init in every SDL2 version (the hint
        // only from 2.0.22).
        for (int i = 0; i < SDL_GetNumVideoDrivers() && !up; ++i) {
            const std::string name = SDL_GetVideoDriver(i);
            if (name == "dummy" || name == "offscreen" || name == "evdev")
                continue;   // no display
            SDL_setenv("SDL_VIDEODRIVER", name.c_str(), 1);
            up = SDL_InitSubSystem(flags) == 0;
        }
        if (!up) {
            std::fprintf(stderr, "video: no driver started: %s\n", first.c_str());
            return false;
        }
    }
    const char* driver = SDL_GetCurrentVideoDriver();
    std::printf("video: driver %s\n", driver != nullptr ? driver : "?");
    return true;
}

bool poll_screen_events(SDL_Window* win) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (handle_fullscreen_toggle(ev, win)) continue;
        if (ev.type == SDL_QUIT) return false;
    }
    return true;
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

DosTicker::DosTicker() : freq_(SDL_GetPerformanceFrequency()) {
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
    const double now = now0;
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

void maybe_auto_fullscreen(SDL_Window* win, int frame) {
    if (win != nullptr && frame == env_int("OLDUVAI_AUTO_FULLSCREEN", -1))
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
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
    // Decode the embedded logo for the running window's icon (Linux WMs read it
    // from the window; on macOS SDL sets the Dock icon; Finder uses the .icns).
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
