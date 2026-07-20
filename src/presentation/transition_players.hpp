// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Blocking screen-change transition/cinematic players (OL-B3).
//
// Extracted from run_platform_level (game_app.cpp): the three players that
// take over the frame loop for the sub-second screen-change animations —
//   * play_transition        — classic 320-window pan / fade / secret slides
//   * play_transition_wide   — the same kinds over WIDE native buffers
//   * play_panorama_wide     — the continuous 4-screen strip pan (kind 1)
//
// They are free functions over TransitionShellCtx — a narrow view of the
// shell state they genuinely read.  Everything that owns SDL textures, the
// widescreen cache or the private `Loaded` aggregate stays in game_app and
// is reached through the ctx callbacks (present/pacing + the three screen
// compose helpers).  BEHAVIOR-PRESERVING move: the player internals are
// verbatim from game_app.cpp; none of them touches core::global_rng() (the
// compose helpers behind the callbacks are documented RNG-free — see
// compose_surface_screen_static's contract in game_app.cpp).
#pragma once

#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "presentation/game_render.hpp"

namespace olduvai::presentation {

// Narrow context for the blocking transition players.  Built by
// run_platform_level right before the transition dispatch, once per played
// transition — so the by-value fields (slide geometry, widescreen metrics)
// carry that frame's values, and pace_last starts at 0 exactly like the old
// per-frame `Uint32 pace_last = 0;` local.
struct TransitionShellCtx {
    // Session / loop state.
    SDL_Window* win = nullptr;
    bool* running = nullptr;        // SDL_QUIT inside a player aborts the app
    std::FILE* draw_log = nullptr;  // harness: kind-4 arc overlay trace (JSONL)

    // Pacing.
    Uint32 frame_ms = 1000 / 18;    // 18 Hz logic step
    bool smooth_motion = false;     // opts.enhance.smooth_motion
    Uint32 pace_last = 0;           // paced() metronome state (see .cpp)

    // HD / widescreen presentation parameters.
    bool hd = false;
    int hd_scale = 1;
    const std::string* hd_profile = nullptr;   // opts.hd_profile
    int ws_margin = 0;
    int ws_native_w = 320;
    bool ws_backdrop_ok = false;
    const FrameBuffer* ws_backdrop = nullptr;  // level FOND (valid w/ _ok)
    enhance::HdAssetCache* hd_cache = nullptr; // g.hd_cache (RenderTarget)

    // Live game state/render — read-only in the players.
    const systems::SystemsState* state = nullptr;   // g.state
    const LevelRenderAssets* render = nullptr;      // g.render
    int screen_count = 0;                           // g.tiles.screens.size()

    // Enhanced secret-slide geometry (kinds 3/4).
    int slide_secret_exit_x = 0;   // departure x (= state.secret_exit_x)
    int slide_end_x = 0;           // surface resume position
    int slide_end_y = 0;

    // Callbacks into the shell (own the SDL textures, the widescreen cache
    // and the TU-private Loaded&).
    std::function<void(FrameBuffer&)> upload_and_show;
    std::function<void(std::vector<std::uint8_t>& wide, bool with_hud,
                       bool pre_upscaled)>
        present_wide_transition;
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

// Classic (320-window) transition playback: kind 1 pan-scroll, kind 2 fade
// pair, kinds 3/4 enhanced secret slides (kind 4 with the player jump-arc
// overlay).  Presents via ctx.upload_and_show.
void play_transition(TransitionShellCtx& ctx, const FrameBuffer& oldf,
                     FrameBuffer& newf, int kind, char dir);

// Widescreen transition playback (§8.7): the same kinds over WIDE native
// buffers, presented through ctx.present_wide_transition.
void play_transition_wide(TransitionShellCtx& ctx,
                          std::vector<std::uint8_t>& oldw,
                          std::vector<std::uint8_t>& neww, int kind, char dir);

// Widescreen PANORAMA pan (kind-1 surface scroll): slide a (320+2M) window
// across a continuous native strip of the four screens involved.
void play_panorama_wide(TransitionShellCtx& ctx, int old_s, int new_s,
                        const FrameBuffer& new_center);

}  // namespace olduvai::presentation
