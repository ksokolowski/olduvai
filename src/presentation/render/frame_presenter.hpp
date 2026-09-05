// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The single per-frame upload/composite/present pipeline, lifted out of
// run_platform_level (game_app.cpp).  Uploads the frame (native or HD-upscaled),
// pillarboxes under widescreen, then runs ONE output-resolution vector-text pass
// for the HUD text, the --cheats picker, and the pause/confirm menus (they must
// share a pass or a second begin/flush would not composite).  Holds POINTERS to
// the run-loop locals (so it always sees live values, like the old [&] lambda)
// plus the three sibling render callbacks it invokes.  Populate once before the
// loop; the FsPresentTimer stays in the caller's thin wrapper.
#pragma once

#include "presentation/menu/cheat_picker.hpp"
#include "presentation/render/level_surface.hpp"
#include "presentation/render/logical_size.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace olduvai::enhance {
class HdText;
}
namespace olduvai::systems {
struct SystemsState;
}

namespace olduvai::presentation {

struct FrameBuffer;
class WidescreenPresenter;
class TextOverlay;
class PauseService;

struct FramePresenter {
    // The level's surface: renderer, texture, vector font, overlay, logical
    // size, and the HD settings.  These were NINE separate members here, copied
    // out of run_platform_level's prologue one at a time — and the same nine
    // appear in WidescreenShellCtx and DescentCtx (§3.7 cluster 1).
    //
    // The hd_profile note that used to live here is worth keeping, because it
    // is why this must be the OBJECT and not a snapshot: the profile is a LIVE
    // Options setting (menus.json marks it "live"; settings_apply returns
    // ApplyTier::Live for a same-scale swap), so the pause overlay / loading /
    // tally upscales have to see a mid-level change. Holding it by value froze
    // it at loop entry and left those composites on the old profile for the
    // rest of the level.  LevelSurface hands out `const std::string*` for the
    // same reason WidescreenPresenter and TransitionPlayers do.
    LevelSurface* surface = nullptr;
    WidescreenPresenter* wsp = nullptr;
    // For draw_hud_for(): the level's charset + sprites/palette and the HD
    // asset cache (make_render_target needs it).  §3.7 cluster 3 slice 2 —
    // the HUD-compose wrapper moved here from a 40-line prologue lambda; the
    // presenter already owned the state and surface it reads.
    const LevelRenderAssets* render = nullptr;
    const std::vector<formats::Sprite>* charset = nullptr;
    enhance::HdAssetCache* hd_cache = nullptr;
    PauseService* pause = nullptr;
    systems::SystemsState* state = nullptr;
    const CheatPicker* cheats = nullptr;
    std::string* menu_shot_path = nullptr;
    // Sibling render callbacks (game_app lambdas): the classic-buffer cheat
    // picker, the HD vector cheat rows, and the non-widescreen banner text.
    std::function<void(FrameBuffer&)> draw_cheat_rows_native;
    std::function<void(std::vector<std::uint8_t>&, int, int)> draw_cheat_rows;
    std::function<void(std::vector<std::uint8_t>&, int, int)> draw_enhanced_banners;

    void present(FrameBuffer& f, bool with_hud = true, bool do_present = true);

    // Draw the gameplay HUD onto `target` — the HD-aware wrapper of draw_hud.
    // Classic: bitmap HUD straight onto the buffer.  HD: draw_hud runs on a
    // native scratch so its STATE MUTATIONS (food cap writeback, GET READY
    // counter decrement) happen exactly once per tick, then the GET READY
    // banner is re-drawn at HD unless the vector-banner path owns it.  The
    // mutation ordering is oracle-relevant; this is a code MOVE, the call
    // order at every site is unchanged.
    void draw_hud_for(FrameBuffer& target);

  private:
    FrameBuffer hud_scratch_{};   // native 320x200 — state-mutation scratch
};

}  // namespace olduvai::presentation
