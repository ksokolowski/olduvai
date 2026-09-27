// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Blocking screen-change transition players:
//   * play_transition        classic 320-window pan / fade / secret slides
//   * play_transition_wide   the same kinds over wide native buffers
//   * play_panorama_wide     the continuous 4-screen strip pan (kind 1)
// Free functions over TransitionShellCtx; SDL textures, the widescreen cache
// and `Loaded` stay in game_app behind the ctx callbacks.  None touches
// core::global_rng() (the compose callbacks are RNG-free).
#pragma once

#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <functional>

#include "presentation/render/widescreen_presenter.hpp"
#include <string>
#include <vector>

#include "presentation/render/game_render.hpp"
#include "presentation/sequence/secret_slide.hpp"         // SlideLanding
#include "presentation/sequence/transition_geometry.hpp"   // TransitionKind

namespace olduvai::presentation {

// Context built per played transition, so the by-value fields carry that
// frame's values and pace_last starts at 0.
struct TransitionShellCtx {
    // Session / loop state.
    SDL_Window* win = nullptr;
    bool* running = nullptr;        // SDL_QUIT inside a player aborts the app
    std::FILE* draw_log = nullptr;  // harness: the exit arc's trace (JSONL)

    // Pacing.
    Uint32 frame_ms = 1000 / 18;    // 18 Hz logic step
    bool smooth_motion = false;     // opts.enhance.smooth_motion
    Uint32 pace_last = 0;           // paced() metronome state (see .cpp)

    // HD / widescreen presentation parameters.
    bool hd = false;
    int hd_scale = 1;
    const std::string* hd_profile = nullptr;   // opts.hd_profile
    // Widescreen geometry is read from its owner (a resize can move the margin
    // mid-transition).
    WidescreenPresenter* wsp = nullptr;
    enhance::HdAssetCache* hd_cache = nullptr; // g.hd_cache (RenderTarget)

    // Live game state/render — read-only in the players.
    const systems::SystemsState* state = nullptr;   // g.state
    const LevelRenderAssets* render = nullptr;      // g.render
    int screen_count = 0;                           // g.tiles.screens.size()

    SlideLanding landing;   // the secret exit's arc (secret_slide.hpp)

    // Callbacks into the shell (own the SDL textures, the widescreen cache
    // and the TU-private Loaded&).
    std::function<void(FrameBuffer&)> upload_and_show;
    // (present_wide_transition was a std::function whose whole body was
    //  `wsp.present_transition(...)`; the players call it through `wsp` now.)
    std::function<RenderTarget(FrameBuffer&)> make_rt;
    // compose_surface_screen_static(g, screen, out, nullptr, nullptr,
    //                               frozen_full) — panorama slot fills.
    std::function<void(int screen, FrameBuffer& out, bool frozen_full)>
        compose_static;
    // compose_surface_screen_wide_native(g, screen, margin, backdrop, wide)
    // — the panorama's pixel-identical steady-margin edge slots.
    std::function<void(int screen, int margin, const FrameBuffer* backdrop,
                       std::vector<std::uint8_t>& wide)>
        compose_wide_native;
    // build_surface_screen_assets(g, screen, ra, st) — seam-straddling tile
    // collection for the panorama strip.
    std::function<void(int screen, LevelRenderAssets& ra,
                       systems::SystemsState& st)>
        build_assets;
};

// Classic (320-window) playback: the pan, the fade pair, the secret slides
// (the exit with its arc).  Presents via ctx.upload_and_show.
void play_transition(TransitionShellCtx& ctx, const FrameBuffer& oldf,
                     FrameBuffer& newf, TransitionKind kind, char dir);

// Widescreen transition playback: the same kinds over WIDE native
// buffers, presented through ctx.present_wide_transition.
void play_transition_wide(TransitionShellCtx& ctx,
                          std::vector<std::uint8_t>& oldw,
                          std::vector<std::uint8_t>& neww, TransitionKind kind,
                          char dir);

// Widescreen PANORAMA pan (kind-1 surface scroll): slide a (320+2M) window
// across a continuous native strip of the four screens involved.
void play_panorama_wide(TransitionShellCtx& ctx, int old_s, int new_s,
                        const FrameBuffer& new_center);

}  // namespace olduvai::presentation
