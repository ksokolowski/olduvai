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

// OLDUVAI_DUMP_STEADY debug aid: on each steady (non-widescreen) present, dump
// the tightly-packed RGBA32 buffer as steady_fb_NNNN.bmp into the env-var
// directory.  No-op when OLDUVAI_DUMP_STEADY is unset.  `pixels` is only read.
void maybe_dump_steady(const void* pixels, int w, int h);

// OLDUVAI_DUMP_OUTPUT debug aid: the renderer's FINAL output — HD scaler,
// widescreen margins AND the vector HUD text, which the steady dumps miss
// because it is drawn later, into the overlay — as out_NNNNN.bmp.  Call it
// before SDL_RenderPresent (a post-present read is black on Metal).  Capped at
// 60 dumps per second, so a VGA-scan present at a 120 Hz display does not fill
// the disk.  (A cap BELOW the present rate is worse than it looks: "30 since
// the last dump" against ~37 Hz smooth presents kept every OTHER one — 18 Hz.)
// No-op when unset.
void maybe_dump_output(SDL_Renderer* ren);

// Every frame-dump hook above (and the widescreen steady dump) appends
// "<file> <SDL performance counter>" to <dir>/frames.txt, so a clip can be
// timed against an OLDUVAI_AUDIO_CAPTURE recording (audio.hpp) instead of
// assuming the run kept a fixed frame rate.  The consumer is the owner's
// device-preview script, kept out of the public tree (.gitattributes).
void note_dump_time(const char* dir, const char* file);

}  // namespace olduvai::presentation
