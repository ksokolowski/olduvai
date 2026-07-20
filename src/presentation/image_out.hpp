// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Screenshot writer: PNG when the path ends ".png", BMP otherwise.
#pragma once

#include <SDL.h>

#include <string>

namespace olduvai::presentation {

bool save_surface_image(SDL_Surface* surface, const std::string& path);

}  // namespace olduvai::presentation
