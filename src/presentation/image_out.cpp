// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/image_out.hpp"

#include <cctype>

#define STB_IMAGE_WRITE_IMPLEMENTATION
// Vendored single-header: silence its own warnings under -Werror.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

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

}  // namespace olduvai::presentation
