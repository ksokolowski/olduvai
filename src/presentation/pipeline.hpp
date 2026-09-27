// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The session's output pipeline, as the title menu and the platform level
// see it: the window and renderer, the audio device, and the way to change
// them.  `adopt` may replace all three; read them at use time, never cache.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "presentation/menu/settings_apply.hpp"   // DisplaySettings
#include "presentation/window_util.hpp"           // ScaledWindow

namespace olduvai::presentation {

class SdlAudio;

struct Pipeline {
    ScaledWindow& sw;
    std::unique_ptr<SdlAudio>& audio;
    // The session's settings take `settings`; the window and the audio are
    // rebuilt as needed.  `aspect`: only the boss carries it (the menus
    // apply aspect live).  False: a rebuild failed and the program ends.
    std::function<bool(const DisplaySettings& settings,
                       const std::string* aspect)>
        adopt;
};

}  // namespace olduvai::presentation
