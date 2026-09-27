// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// After a won boss fight: the victory cinematic, the fade and the tally.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "presentation/boss/boss_fight.hpp"
#include "presentation/boss_app.hpp"                   // BossRunResult
#include "presentation/render/boss_arena.hpp"
#include "presentation/render/boss_render.hpp"         // BossAssets
#include "presentation/render/game_render.hpp"
#include "presentation/render/level_surface.hpp"
#include "presentation/render/smooth_present.hpp"      // SmoothPacer
#include "presentation/sequence/screen_presenter.hpp"
#include "presentation/sequence/screens.hpp"           // PresentFn
#include "presentation/sequence/text_screen_present.hpp"

namespace olduvai::enhance { class HdAssetCache; }

namespace olduvai::presentation {

class SdlAudio;

// What the post-fight phases share: the presentation surface and arena,
// the text-screen presenter, the fight's assets, player and framebuffer, and
// the result they update.
struct BossEnding {
    LevelSurface& surface;
    enhance::HdAssetCache& hd_cache;
    BossArenaPresenter& arena;
    ScreenPresenter& screen;
    const TextScreenDeps& text_screen_deps;
    const PresentFn& lpresent;
    const BossAssets& assets;
    BossPlayerState& player;
    FrameBuffer& fb;
    const std::string& shot;
    std::uint32_t frame_ms;
    BossRunResult& res;
    SdlAudio* audio;
    const std::filesystem::path& game_dir;
    bool enhanced;
    RenderTarget target(FrameBuffer& b) const {
        return make_render_target(b, surface, hd_cache);
    }
};

// After a won fight each boss main calls Level_EndScreen(N, 500) (L2
// 23cf:0fc9, L4 24cc:0818, L6 254f:0620): the victory cinematic, then the fade
// to the tally.  ESC is inert throughout (no menu); only SDL_QUIT stops it.
void play_boss_ending(const BossEnding& c, BossFight& f,
                      const std::function<void(RenderTarget&)>& victory_sprites,
                      int& l2_last_flash, bool smooth, SmoothPacer& pacer,
                      SmoothPos& sp, bool with_cinematic);

}  // namespace olduvai::presentation
