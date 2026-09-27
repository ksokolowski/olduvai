// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Screenshot writer: PNG when the path ends ".png", BMP otherwise.
#pragma once

#include <SDL.h>

#include <string>

namespace olduvai::presentation {

bool save_surface_image(SDL_Surface* surface, const std::string& path);

// Save a packed RGBA32 buffer (pitch w*4) to `path` via save_surface_image.
// `pixels` is read only.  True on success.
bool save_rgba_image(const void* pixels, int w, int h, const std::string& path);

// Capture the renderer's full output (rendered, not yet presented: black on
// Metal after) and save it to `path`.  True on success.
bool capture_renderer_output(SDL_Renderer* ren, const std::string& path);

// OLDUVAI_DUMP_STEADY: dump each steady non-widescreen present's RGBA32 buffer
// as steady_fb_NNNN.bmp.  No-op when unset.
void maybe_dump_steady(const void* pixels, int w, int h);

// OLDUVAI_DUMP_OUTPUT: the renderer's final output (HD scaler, margins, vector
// HUD text) as out_NNNNN.bmp.  Call before SDL_RenderPresent.  At most 60 per
// second (a cap below the present rate drops every other frame).  No-op when
// unset.
void maybe_dump_output(SDL_Renderer* ren);

// maybe_dump_output, then SDL_RenderPresent, for present sites without a
// presenter (boss arena, text screens).
void present_output(SDL_Renderer* ren);

// Every dump hook appends "<file> <SDL performance counter>" to
// <dir>/frames.txt, so a clip can be timed against an OLDUVAI_AUDIO_CAPTURE
// recording.
void note_dump_time(const char* dir, const char* file);

}  // namespace olduvai::presentation
