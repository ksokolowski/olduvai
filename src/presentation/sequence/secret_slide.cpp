// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/sequence/secret_slide.hpp"

#include <algorithm>
#include <vector>

#include "presentation/level/level_state.hpp"
#include "presentation/render/frame_presenter.hpp"
#include "presentation/render/widescreen_presenter.hpp"
#include "presentation/sequence/screen_change.hpp"        // TransitionState
#include "presentation/sequence/transition_players.hpp"
#include "systems/secret.hpp"                              // kSecretFloorY
#include "systems/sprite_ids.hpp"                          // kSprPlayerJump

namespace olduvai::presentation {

SecretSlide::SecretSlide(TransitionKind kind, const SlideLanding& at,
                         bool facing_left)
    : frames_(kind == TransitionKind::kSecretEntry ? 12 : 30),
      arc_frames_(kind == TransitionKind::kSecretExit ? 26 : 0),
      at_(at),
      flip_(at.end_x < at.exit_x || (at.end_x == at.exit_x && facing_left)) {}

void SecretSlide::draw_arc(RenderTarget& rt, const LevelRenderAssets& a,
                           double pos, int f2, std::FILE* log) const {
    const double t = std::min(1.0, pos / frames_);
    const ArcOverlay ao = arc_overlay_pos(
        {frames_, arc_frames_, at_.exit_x, at_.end_x, at_.end_y, kBakeY,
         kArcPeak},
        pos, t);
    if (log != nullptr)
        std::fprintf(log,
                     "{\"trans\":%d,\"f2\":%d,\"of\":%d,"
                     "\"px\":%d,\"py\":%d,\"resx\":%d,\"resy\":%d,"
                     "\"exitx\":%d}\n",
                     static_cast<int>(TransitionKind::kSecretExit), f2,
                     total(), ao.x, ao.y, at_.end_x, at_.end_y, at_.exit_x);
    constexpr int spr = systems::kSprPlayerJump;
    if (spr < static_cast<int>(a.entity_sprites.size()))
        blit_sprite(rt, a.entity_sprites[spr], a.palette, ao.x, ao.y, flip_);
}

SlideLanding slide_landing(const systems::SystemsState& st) {
    return {st.secret_exit_x, st.player.x, st.player.y};
}

ShownFields room_as_left(const systems::SystemsState& st,
                         const SlideLanding& at) {
    ShownFields room = ShownFields::of(st);
    room.px = at.exit_x;
    room.py = systems::kSecretFloorY - 30;
    room.secret = 1;
    return room;
}

void play_secret_slide(Loaded& g, TransitionState& trans, FramePresenter& fp,
                       FrameBuffer& fb, TransitionShellCtx& tctx,
                       const std::function<void(RenderTarget&)>& bubbles) {
    if (trans.wide && trans.slide_old_wide_ok) {
        // The new wide frame from the player-less 320 centre (the entry
        // self-tiles, the exit peeks).
        FrameBuffer nc{};
        {
            RenderTarget rt{nc.px.data(), 320, 200, 1, nullptr, nullptr};
            rt.advance_state = false;
            compose_frame(rt, g.state, g.render, /*draw_player=*/false,
                          bubbles);
        }
        std::vector<std::uint8_t> new_wide;
        fp.wsp->wrap_wide(nc, new_wide);
        play_transition_wide(tctx, trans.slide_old_wide, new_wide, trans.kind,
                             trans.dir);
        return;
    }
    FrameBuffer fb_noplayer{fb.w, fb.h};
    {
        auto rt = tctx.make_rt(fb_noplayer);
        compose_frame(rt, g.state, g.render, /*draw_player=*/false, bubbles);
    }
    fp.draw_hud_for(fb_noplayer);
    play_transition(tctx, trans.old_frame, fb_noplayer, trans.kind,
                    trans.dir);
}

}  // namespace olduvai::presentation
