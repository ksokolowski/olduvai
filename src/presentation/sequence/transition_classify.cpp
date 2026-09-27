// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/sequence/transition_classify.hpp"

#include <cstdlib>

#include "systems/screen_topology.hpp"

namespace olduvai::presentation {

TransitionChoice classify_transition(const PrevFrame& pf,
                                     const systems::SystemsState& st,
                                     bool enhanced, bool warp_fade) {
    const bool now_inside = st.cave_flag || st.secret_flag;
    const bool l7_fake_cave =
        std::abs(pf.screen - st.current_screen) == 1 &&
        systems::seam_kind(st.current_level, pf.screen, st.current_screen) ==
            systems::SeamKind::FakeCaveInstant;
    if (l7_fake_cave && enhanced) warp_fade = true;
    TransitionChoice c;
    if (!now_inside && !pf.inside && !warp_fade) {
        c.kind = TransitionKind::kPan;
        const int ddx = st.player.x - pf.px;
        const int ddy = st.player.y - pf.py;
        if (std::abs(ddx) >= std::abs(ddy))
            c.dir = ddx < 0 ? 'R' : 'L';
        else
            c.dir = ddy < 0 ? 'D' : 'U';
    } else if (enhanced && !pf.inside && now_inside && st.secret_flag) {
        c.kind = TransitionKind::kSecretEntry;
    } else if (enhanced && pf.secret && !now_inside) {
        c.kind = TransitionKind::kSecretExit;
    } else {
        c.kind = TransitionKind::kFadePair;
    }
    return c;
}

bool take_warp_fade(systems::SystemsState& st) {
    const bool fade = st.player.cave_warp_freeze == 0x3E8 ||
                      st.player.cave_warp_pending;
    st.player.cave_warp_pending = false;
    return fade;
}

bool is_l3_trunk_descent(const PrevFrame& pf, const systems::SystemsState& st) {
    return pf.screen == 17 && st.current_screen == 18 &&
           systems::seam_kind(st.current_level, 17, 18) ==
               systems::SeamKind::TrunkDescent;
}

ShownFields ShownFields::of(const systems::SystemsState& st) {
    const systems::PlayerState& p = st.player;
    ShownFields f;
    f.px = p.x;
    f.py = p.y;
    f.screen = st.current_screen;
    f.cave = st.cave_flag;
    f.cave_index = st.cave_index;
    f.secret = st.secret_flag;
    f.psprite = p.sprite;
    f.pdx = p.dx;
    f.pdy = p.dy;
    f.pfacing = p.facing_left;
    f.pclub = p.club_flag;
    f.emerge = st.cave_emerge_frames;
    return f;
}

ShownFields ShownFields::before(const PrevFrame& pf) {
    ShownFields f;
    f.px = pf.px;
    f.py = pf.py;
    f.screen = pf.screen;
    f.cave = pf.cave ? 1 : 0;
    f.cave_index = pf.cave ? pf.cave_index : -1;   // the exit set -1
    f.secret = pf.secret ? 1 : 0;
    f.psprite = pf.psprite;
    f.pdx = pf.pdx;
    f.pdy = pf.pdy;
    f.pfacing = pf.pfacing;
    f.pclub = pf.pclub;
    f.emerge = pf.emerge;
    return f;
}

void ShownFields::apply(systems::SystemsState& st) const {
    systems::PlayerState& p = st.player;
    p.x = px;
    p.y = py;
    st.current_screen = screen;
    st.cave_flag = cave;
    st.cave_index = cave_index;
    st.secret_flag = secret;
    p.sprite = psprite;
    p.dx = pdx;
    p.dy = pdy;
    p.facing_left = pfacing;
    p.club_flag = pclub;
    st.cave_emerge_frames = emerge;
}

}  // namespace olduvai::presentation
