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
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
        0, ow, oh, 32, SDL_PIXELFORMAT_RGBA32);
    bool ok = false;
    if (s != nullptr &&
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_RGBA32,
                             s->pixels, s->pitch) == 0) {
        ok = save_surface_image(s, path);
    }
    if (s != nullptr) SDL_FreeSurface(s);
    return ok;
}

void maybe_dump_steady(const void* pixels, int w, int h) {
    const char* dir = std::getenv("OLDUVAI_DUMP_STEADY");
    if (dir == nullptr) return;
    static int seq = 0;
    char path[512];
    std::snprintf(path, sizeof path, "%s/steady_fb_%04d.bmp", dir, seq++);
    save_rgba_image(pixels, w, h, path);
}

}  // namespace olduvai::presentation
