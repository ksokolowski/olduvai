// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The display of a platform level or a boss fight: its surface, opened first
// (a level's bind-time decisions key on the vector-HUD gate), and the view
// over it.  rebuild() is the in-place display reinit, one sequence for both.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include <SDL.h>

#include "presentation/game_app.hpp"              // GameOptions
#include "presentation/menu/settings_apply.hpp"   // DisplaySettings
#include "presentation/pipeline.hpp"
#include "presentation/render/level_surface.hpp"
#include "presentation/render/logical_size.hpp"

namespace olduvai::presentation {

// What a rebuild replaced.  kAudioToo: the audio device as well (its music
// stopped).
enum class Rebuilt { kFailed, kDisplay, kAudioToo };

template <class View, class Deps>
class Display {
public:
    // The logical size a new surface starts at, from the options.
    using InitialSize = LogicalDims (*)(const GameOptions&);

    Display(Pipeline& pipe, const GameOptions& opts, InitialSize initial)
        : pipe_(pipe), opts_(opts), initial_(initial) {
        open_surface();
    }
    ~Display() = default;
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    LevelSurface& surface() { return *surface_; }
    View& view() { return *view_; }
    void open_view(const Deps& deps) {
        deps_ = std::make_unique<Deps>(deps);
        view_ = std::make_unique<View>(*surface_, *deps_);
    }

    // Adopt `target` (and `aspect`, where it is reinit-class: the boss) and
    // rebuild: the view and the surface go first (their textures belong to
    // the renderer the adopt may destroy), then the new surface,
    // `reload(use_hd_text)` for the data the display decided at bind time,
    // the new view.
    Rebuilt rebuild(const DisplaySettings& target, const std::string* aspect,
                    const std::function<bool(bool use_hd_text)>& reload) {
        const std::uint32_t t0 = SDL_GetTicks();
        const bool audio = target.music_device != opts_.music_device ||
                           target.sfx_backend != opts_.sfx_backend;
        view_.reset();
        surface_.reset();
        if (!pipe_.adopt(target, aspect)) return Rebuilt::kFailed;
        open_surface();
        if (!reload(surface_->use_hd_text())) return Rebuilt::kFailed;
        view_ = std::make_unique<View>(*surface_, *deps_);
        std::fprintf(stderr, "display: rebuilt in place at scale %d%s (%u ms)\n",
                     surface_->hd_scale(), audio ? ", audio reopened" : "",
                     static_cast<unsigned>(SDL_GetTicks() - t0));
        return audio ? Rebuilt::kAudioToo : Rebuilt::kDisplay;
    }

private:
    // HD and the vector HUD/menu require --enhanced; an hd_profile alone must
    // not force HD.
    void open_surface() {
        surface_ = std::make_unique<LevelSurface>(
            pipe_.sw.win, pipe_.sw.ren,
            hd_active(opts_.enhanced, opts_.hd_profile),
            hd_scale_for(opts_.enhanced, opts_.hd_profile, opts_.render_scale),
            opts_.hd_font, opts_.hd_profile, initial_(opts_));
    }

    Pipeline& pipe_;
    const GameOptions& opts_;
    InitialSize initial_;
    std::unique_ptr<LevelSurface> surface_;
    std::unique_ptr<Deps> deps_;
    std::unique_ptr<View> view_;
};

}  // namespace olduvai::presentation
