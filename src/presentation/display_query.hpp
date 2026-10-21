// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The desktop's size, for callers that must not include <SDL.h>: main.cpp,
// where on Windows SDL.h renames main() to SDL_main.  window_util.cpp.
#pragma once

namespace olduvai::presentation {

// Display 0's desktop mode into w x h; false when video is unavailable.
// Leaves the SDL video subsystem initialised (the session's SDL_Init counts
// on top of it).
bool desktop_display_size(int& w, int& h);

}  // namespace olduvai::presentation
