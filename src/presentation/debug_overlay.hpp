// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Dev/debug visualization overlays — collision tint, entity boxes, perf text.
//
// These are NOT byte-parity with the Python reference's debug overlays;
// they are genuinely-useful render-path aids drawn into the gameplay frame
// buffer just before it is presented.  Every draw is gated by the caller on
// the corresponding --debug-* flag so default rendering is untouched.
//
// The overlays operate on the RGBA gameplay FrameBuffer.  `scale` is the HD
// render scale (1 in classic mode, hd_scale otherwise) so coordinates and
// rectangles map onto the (possibly upscaled) buffer.  Text (--debug-perf)
// uses the 1bpp bitmap font via draw_text, which targets a native 320x200
// buffer, so the perf overlay text is only emitted at scale == 1 (classic);
// the boxes/tint render at any scale.

#pragma once

#include <string>
#include <vector>

#include "presentation/game_render.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

// Tint every solid collision cell (state.collision.test(x,y) == false) with a
// translucent magenta, so the collision map is visible over the scene.
void draw_debug_collision(FrameBuffer& fb, const systems::SystemsState& state,
                          int scale);

// Ring each active+visible entity with a bright circle centred on its draw
// rect (e.x, e.y + sprite dimensions from entity_sprites[e.sprite]), so
// objects are unmistakably marked on bug-report screenshots.
void draw_debug_entities(FrameBuffer& fb, const systems::SystemsState& state,
                         const std::vector<formats::Sprite>& entity_sprites,
                         int scale);

// Draw FPS + frame-time (ms) text at the top-left corner using the bitmap
// font.  Only renders at scale == 1 (draw_text is native-320x200 only).
void draw_debug_perf(FrameBuffer& fb,
                     const std::vector<formats::Sprite>& charset,
                     const std::vector<formats::Rgb>& palette,
                     double fps, double frame_ms, int scale);

}  // namespace olduvai::presentation
