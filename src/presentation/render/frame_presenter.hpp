// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The per-frame upload/composite/present pipeline for platform levels: upload
// the frame (native or HD), pillarbox under widescreen, then one
// output-resolution text pass for the HUD, the cheat picker and the pause /
// confirm menus (they must share a pass: a second begin/flush would not
// composite).  Pointers to run-loop state, wired once before the loop.
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
struct EnhancedHudLayout;
}
namespace olduvai::systems {
struct SystemsState;
}

namespace olduvai::presentation {

struct FrameBuffer;
struct FrameStats;
class WidescreenPresenter;
class TextOverlay;
class Menu;
class ConfirmDialog;

struct FramePresenter {
    // The level's surface.  Held as the object, not copies: hd_profile is a
    // live Options setting, and the pause / loading / tally upscales must see a
    // mid-level change.
    LevelSurface* surface = nullptr;
    WidescreenPresenter* wsp = nullptr;
    // For draw_hud_for(): charset, sprites/palette and the HD asset cache.
    const LevelRenderAssets* render = nullptr;
    const std::vector<formats::Sprite>* charset = nullptr;
    enhance::HdAssetCache* hd_cache = nullptr;
    systems::SystemsState* state = nullptr;
    const CheatPicker* cheats = nullptr;
    std::string* menu_shot_path = nullptr;
    // Game_app callbacks: the classic cheat picker, the HD cheat rows.
    std::function<void(FrameBuffer&)> draw_cheat_rows_native;
    std::function<void(const enhance::Canvas&)> draw_cheat_rows;

    // OLDUVAI_FRAME_STATS sink; present() records through FrameStats::Timer and
    // note_present() (no-ops when the env var is off).
    FrameStats* stats = nullptr;    // &diag.stats

    std::function<void(const enhance::Canvas&)> draw_enhanced_banners;

    void present(FrameBuffer& f, bool with_hud = true, bool do_present = true);

    // The paused frame: the frozen scene (no state advance), wrapped wide on
    // the widescreen present path, the menu or its confirm over it,
    // presented.  `shot`: OLDUVAI_PAUSE_SHOT's path, or null.
    void present_paused(const Menu& menu, const ConfirmDialog& confirm,
                        const char* shot);

    // Draw the gameplay HUD onto `target`.  Classic: the bitmap HUD directly.
    // HD: draw_hud runs on a native scratch so its state changes (food cap, GET
    // READY decrement) happen once per tick, then GET READY is redrawn at HD
    // unless the vector banner owns it.  The order is oracle-relevant.
    void draw_hud_for(FrameBuffer& target);

  private:
    // Into the texture: a wide frame whole (true), else the HD or classic
    // 320 path.  The HUD bars go into the buffer on the HD paths.
    bool upload(FrameBuffer& f, const enhance::EnhancedHudLayout* hud) const;
    // The texture onto the canvas: whole, pillarboxed in the wide canvas, or
    // plain.
    void show_canvas(bool wide_frame) const;
    // One output-resolution text pass: HUD text, cheat rows, pause menu or
    // confirm (a second begin/flush would not composite).
    void draw_text_pass(const enhance::EnhancedHudLayout* hud);

    FrameBuffer hud_scratch_;   // native 320x200 — state-mutation scratch
    // The pause menu for the text pass; set only inside present_paused.
    const Menu* menu_ = nullptr;
    const ConfirmDialog* confirm_ = nullptr;
    const char* pause_shot_ = nullptr;
};

}  // namespace olduvai::presentation
