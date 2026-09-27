// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The boss arena as a presentation surface: its widescreen geometry and the
// presenter that turns a fight frame into pixels.  The SDL side of
// boss_widescreen.hpp, which stays SDL-free (test_boss_widescreen links
// without SDL).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <SDL.h>

#include "presentation/render/game_render.hpp"
#include "presentation/render/level_surface.hpp"
#include "presentation/render/logical_size.hpp"
#include "presentation/window_util.hpp"

namespace olduvai::enhance { class HdAssetCache; }

namespace olduvai::presentation {

class ConfirmDialog;
class Menu;

class BossHud;
struct FrameStats;

// Boss-arena widescreen state, shared by the fight loop, the victory sequences
// and the fade/tally.  Not WidescreenPresenter: the arena has no screens or
// neighbours; its margins are a mirrored arena with an edge gradient.  The
// wide texture is the surface's.
struct BossWidescreen {
    BossWidescreen(LevelSurface& surface, bool enabled, LogicalDims fallback);
    ~BossWidescreen() = default;
    BossWidescreen(const BossWidescreen&) = delete;
    BossWidescreen& operator=(const BossWidescreen&) = delete;

    // Recompute on an output size change (Alt+Enter, resize); no-op otherwise.
    // Updates lsz too: the text-overlay flush restores SDL's logical size from
    // it, and a stale value squashes the wide buffer.  OLDUVAI_WS_FORCE_MARGIN
    // pins the margin (a no-op under tests).
    void rebuild_if_resized();

    int M = 0;                     // margin, native px each side
    bool active = false;           // widescreen on AND margin > 0
    int w = 320;                   // 320 + 2*M
    // The surface's wide texture at this width; null when inactive.
    SDL_Texture* wtex() const { return active ? surface_.wide_tex(w) : nullptr; }
    // Last native frame sent wide: the post-victory fade's source (the HD `fb`
    // still holds the fight).
    FrameBuffer last_native;

private:
    LevelSurface& surface_;
    bool enabled_;                 // hd && aspect == "widescreen"
    LogicalDims fallback_;
    int ow0_ = 0, oh0_ = 0;        // last seen renderer output size
};

// Wrap a native 320x200 arena frame in the boss margins: a pure reflection of
// the edge strips (black walls stay black) with the 0.10 edge gradient.
void compose_arena_wide(std::vector<std::uint8_t>& out, int M,
                        const FrameBuffer& src);

// Target for every arena compose that is not the live fight frame.  It sets
// advance_state = false: a paused or re-composed frame must not step the
// animation the fight is holding.  origin_x is the margin, so edge sprites
// overflow into it.
RenderTarget boss_visual_target(std::uint8_t* px, int w, int h, int scale,
                                enhance::HdAssetCache* cache,
                                const std::string* profile, int origin_x);

// The smooth-motion triple the live fight feeds a wide compose, so a compose
// that skips it does so visibly.  Inert when use_float is false.
void boss_smooth_pos(RenderTarget& rt, bool use_float, float pfx, float pfy);

// The per-frame present pipeline for a boss fight.  It holds pointers to what
// it displays, not the state around it: no assets, player, l2/l4/l6 or
// internal_level.  Per-boss drawing arrives as callbacks bound once by the
// driver.
class BossArenaPresenter {
public:
    BossArenaPresenter(LevelSurface& surface, BossWidescreen& ws, BossHud& hud,
                       FrameBuffer& fb, enhance::HdAssetCache& cache);

    // ---- what to draw (bound once by the driver) ----
    // The HUD-clean arena background, RGBA 320x200, static for the fight.
    const std::vector<std::uint8_t>* arena_bg = nullptr;
    // Draw this frame's fight sprites into the target
    // (render_l2/l4/l6_sprites).
    std::function<void(RenderTarget&)> draw_fight_sprites;
    // L4 ride-off: take the wide sprite-overflow path, not the mirror.
    std::function<bool()> wide_victory;
    // The ride-off, drawn natively (fade source) and as overflowing sprites.
    std::function<void(RenderTarget&)> draw_victory_native;
    std::function<void(RenderTarget&)> draw_victory_sprites;

    // ---- the HUD's SDL side ----
    // Paint the vector HUD into the output overlay and flush it over the scene.
    void hud_overlay(bool draw_lives);        // centre 320 domain
    void hud_overlay_wide(bool draw_lives);   // wide domain (origin M, width w)

    // ---- the present family ----
    // The 320-wide present: upload `fb`, pillarbox it on a wide canvas, HUD
    // over.
    void present_frame(bool draw_lives = true, bool do_present = true);
    // One wide upscaled fight frame: the cached static wide background, then
    // the sprites at origin_x = M.  Returned rather than shown, for the
    // screenshot.
    std::vector<std::uint8_t> build_wide_up();
    // Show an already-upscaled wide buffer (with HUD).
    void show_wide_up(const std::vector<std::uint8_t>& up, bool draw_lives,
                      bool do_present);
    // One wide native frame (wide_w() x 200) from the clean arena, `draw`
    // painting sprites at origin M.  Native: the fade darkens it per frame and
    // the pause wants a FrameBuffer.  Empty when widescreen is inactive.
    std::vector<std::uint8_t> compose_wide_native(
        const std::function<void(RenderTarget&)>& draw);
    // Upscale one such frame and show it; draw_hud=false for the fade.
    void show_wide_native(const std::vector<std::uint8_t>& wide,
                          bool draw_lives = true, bool do_present = true,
                          bool draw_hud = true);
    // Make SDL's logical canvas the wide one (and lsz with it).
    void use_wide_logical();
    // The last native frame shown wide, for the post-victory fade.  Sequences
    // that compose their own wide frame (L2 flash, L6 finish) hand theirs over.
    const FrameBuffer& last_wide_native() const;
    void keep_fade_source(const FrameBuffer& nat);
    // 320 + 2*margin, for callers that size a buffer.
    int wide_w() const;
    // Widescreen is selected (enhanced, aspect widescreen, margin > 0).
    bool wide_on() const;
    // ...and the wide texture exists.  A resize can drop it mid-sequence, so
    // every caller of this has a 320 fallback.
    bool wide_ready() const;
    // Recompute after a resize (the present family does it itself).
    void rebuild_if_resized();
    void present_wide(bool draw_lives = true, bool do_present = true);
    // Present a native 320x200 frame wide, mirrored like the fight; plain 320
    // upscale if a resize dropped widescreen.
    void present_wide_native(const FrameBuffer& nat, bool draw_lives = true,
                             bool do_present = true, bool draw_hud = true);
    // Route a fight present: wide, the L4 ride-off through the overflow
    // compose, or the 320 path.
    void present_any(bool draw_lives = true, bool do_present = true);
    // A native 320x200 frame, upscaled and pillarboxed inside the margins, and
    // presented (the F5 form over the frozen fight).
    void show_native(const FrameBuffer& f);
    // --play-shot: the fight frame to `path`.  real_output: the window's own
    // output (logical scaling and bars); otherwise the frame at its true size.
    void capture_shot(const std::string& path, bool real_output);
    // The frozen fight under the pause menu (or its confirm dialog), not
    // flipped: the caller presents or reads back.  `render_frame` draws the
    // whole frozen arena natively, `render_sprites` just the sprites for the
    // wide rebuild; the menu art is the charset and the bone pointer.
    void show_pause(const std::function<void(RenderTarget&)>& render_frame,
                    const std::function<void(RenderTarget&)>& render_sprites,
                    const Menu& menu, const ConfirmDialog& confirm,
                    const std::vector<formats::Sprite>& charset,
                    const formats::Sprite* bone,
                    const std::vector<formats::Rgb>* bone_palette);

    // OLDUVAI_FRAME_STATS sink, or null.
    FrameStats* stats = nullptr;

    // The live smooth-motion cells, read (not pushed): the driver rewrites them
    // from several places.  Inert while *use_float is false.
    const bool* smooth_use_float = nullptr;
    const float* smooth_fx = nullptr;
    const float* smooth_fy = nullptr;

private:
    // The live smooth cells onto a target (inert when unset or false).
    void apply_smooth(RenderTarget& rt) const;

    LevelSurface& surface_;
    BossWidescreen& ws_;
    BossHud& hud_;
    FrameBuffer& fb_;
    enhance::HdAssetCache& cache_;

    // The arena background never changes during the fight: its upscaled wide
    // form is built once and copied per frame.  Rebuilt on a margin or HD
    // profile change.
    std::vector<std::uint8_t> bg_hd_;
    int bg_hd_M_ = -1;
    std::string bg_hd_profile_;
};

}  // namespace olduvai::presentation
