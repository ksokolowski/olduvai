// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Screenshot writer: PNG when the path ends ".png", BMP otherwise.
#pragma once

#include <SDL.h>

#include <string>

namespace olduvai::presentation {

bool save_surface_image(SDL_Surface* surface, const std::string& path);

// Wrap a tightly-packed RGBA32 pixel buffer (pitch = w*4) as an SDL surface
// and save it to `path` via save_surface_image.  Collapses the
// CreateRGBSurfaceWithFormatFrom → save → FreeSurface idiom repeated at the
// steady/transition/screenshot dump sites.  `pixels` is only read.  Returns
// true on a successful save.
bool save_rgba_image(const void* pixels, int w, int h, const std::string& path);

// Capture the renderer's full output (the scene must already be rendered
// but NOT yet presented — a post-present RenderReadPixels is black on
// Metal) and write it to `path` via save_surface_image.  Wraps the
// GetRendererOutputSize → RGBA32 surface → RenderReadPixels → save → free
// dance that a dozen screenshot sites repeated verbatim.  Returns true on
// a successful read + save.
bool capture_renderer_output(SDL_Renderer* ren, const std::string& path);

}  // namespace olduvai::presentation
