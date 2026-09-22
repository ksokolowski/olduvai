// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/image_out.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#define STB_IMAGE_WRITE_IMPLEMENTATION
// Vendored single-header: silence its own warnings under -Werror.
#if defined(__GNUC__) || defined(__clang__)   // MSVC: C4068 unknown pragma
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)   // MSVC: C4068 unknown pragma
#pragma GCC diagnostic pop
#endif

namespace olduvai::presentation {

namespace {
bool ends_with_png(const std::string& path) {
    if (path.size() < 4) return false;
    std::string ext = path.substr(path.size() - 4);
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    return ext == ".png";
}
}  // namespace

bool save_surface_image(SDL_Surface* surface, const std::string& path) {
    if (surface == nullptr) return false;
    if (!ends_with_png(path)) {
        return SDL_SaveBMP(surface, path.c_str()) == 0;
    }
    SDL_Surface* rgb = SDL_ConvertSurfaceFormat(
        surface, SDL_PIXELFORMAT_RGB24, 0);
    if (rgb == nullptr) return false;
    const int ok = stbi_write_png(path.c_str(), rgb->w, rgb->h, 3,
                                  rgb->pixels, rgb->pitch);
    SDL_FreeSurface(rgb);
    return ok != 0;
}

bool save_rgba_image(const void* pixels, int w, int h, const std::string& path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<void*>(pixels), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (s == nullptr) return false;
    const bool ok = save_surface_image(s, path);
    SDL_FreeSurface(s);
    return ok;
}

bool capture_renderer_output(SDL_Renderer* ren, const std::string& path) {
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(ren, &ow, &oh);

    // CLEAR THE LOGICAL SIZE FOR THE READ, RESTORE IT AFTER.
    // SDL_RenderReadPixels(rect = nullptr) reads the current VIEWPORT, not the
    // target.  With a logical size set, the viewport is a centred sub-rect, so
    // the read starts at its offset and lands at (0,0) of a full-size surface:
    // the image comes out CROPPED ON THE LEFT with black filling the right,
    // and a reader concludes the game drew it that way.
    //
    // §3.14b found and fixed exactly this in capture_gate_frame (which carries
    // the same comment) and did NOT fix it here — so every F5 bug report taken
    // in a pillarboxed mode has been showing a shifted, cropped frame rather
    // than what the player saw.  Found 2026-09-07 from a widescreen report
    // whose screenshot was itself the misleading evidence.
    int lw = 0, lh = 0;
    SDL_RenderGetLogicalSize(ren, &lw, &lh);
    if (lw != 0 || lh != 0) SDL_RenderSetLogicalSize(ren, 0, 0);

    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
        0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
    bool ok = false;
    if (s != nullptr &&
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                             s->pixels, s->pitch) == 0) {
        ok = save_surface_image(s, path);
    }
    if (s != nullptr) SDL_FreeSurface(s);

    if (lw != 0 || lh != 0) SDL_RenderSetLogicalSize(ren, lw, lh);
    return ok;
}

void maybe_dump_steady(const void* pixels, int w, int h) {
    const char* dir = std::getenv("OLDUVAI_DUMP_STEADY");
    if (dir == nullptr) return;
    static int seq = 0;
    char path[512];
    char name[32];
    std::snprintf(name, sizeof name, "steady_fb_%04d.bmp", seq++);
    std::snprintf(path, sizeof path, "%s/%s", dir, name);
    save_rgba_image(pixels, w, h, path);
    note_dump_time(dir, name);
}

void present_output(SDL_Renderer* ren) {
    maybe_dump_output(ren);
    SDL_RenderPresent(ren);
}

void maybe_dump_output(SDL_Renderer* ren) {
    const char* dir = std::getenv("OLDUVAI_DUMP_OUTPUT");
    if (dir == nullptr || ren == nullptr) return;
    static int seq = 0;
    static Uint64 last = 0;
    const Uint64 now = SDL_GetPerformanceCounter();
    if (last != 0 && (now - last) * 60 < SDL_GetPerformanceFrequency()) return;
    last = now;
    char name[32];
    char path[512];
    std::snprintf(name, sizeof name, "out_%05d.bmp", seq++);
    std::snprintf(path, sizeof path, "%s/%s", dir, name);
    if (capture_renderer_output(ren, path)) note_dump_time(dir, name);
}

void note_dump_time(const char* dir, const char* file) {
    const Uint64 t = SDL_GetPerformanceCounter();
    char path[512];
    std::snprintf(path, sizeof path, "%s/frames.txt", dir);
    std::FILE* f = std::fopen(path, "a");
    if (f == nullptr) return;
    std::fprintf(f, "%s %llu\n", file, static_cast<unsigned long long>(t));
    std::fclose(f);
}

}  // namespace olduvai::presentation
