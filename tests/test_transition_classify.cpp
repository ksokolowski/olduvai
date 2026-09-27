// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// classify_transition — which transition a screen change plays — and the
// state as a frame showed it (ShownFields / ShownAs).  Headless: the frame
// before and the state after, no renderer.

#include "doctest/doctest.h"
#include "presentation/sequence/transition_classify.hpp"

using olduvai::presentation::classify_transition;
using olduvai::presentation::is_l3_trunk_descent;
using olduvai::presentation::PrevFrame;
using olduvai::presentation::ShownAs;
using olduvai::presentation::ShownFields;
using olduvai::presentation::take_warp_fade;
using olduvai::presentation::TransitionKind;
using olduvai::systems::SystemsState;

namespace {

// A surface screen `screen` of `level` with the player at (x, y).
SystemsState at(int level, int screen, int x, int y) {
    SystemsState st;
    st.current_level = level;
    st.current_screen = screen;
    st.player.x = x;
    st.player.y = y;
    return st;
}

TransitionKind kind_of(const SystemsState& before, const SystemsState& after,
                       bool enhanced, bool warp_fade = false) {
    return classify_transition(PrevFrame(before), after, enhanced, warp_fade)
        .kind;
}

}  // namespace

TEST_CASE("surface to surface pans, in the direction the player went") {
    const SystemsState before = at(1, 2, 310, 120);
    for (const bool enhanced : {false, true}) {
        const auto right = classify_transition(PrevFrame(before),
                                               at(1, 3, 4, 120), enhanced,
                                               false);
        CHECK(right.kind == TransitionKind::kPan);
        CHECK(right.dir == 'R');
        const auto left = classify_transition(PrevFrame(at(1, 3, 4, 120)),
                                              at(1, 2, 310, 120), enhanced,
                                              false);
        CHECK(left.dir == 'L');
    }
    // Vertical: the larger move wins.
    CHECK(classify_transition(PrevFrame(at(1, 4, 150, 190)),
                              at(1, 5, 152, 8), false, false)
              .dir == 'D');
}

TEST_CASE("a cave warp fades instead of panning") {
    CHECK(kind_of(at(1, 2, 310, 120), at(1, 3, 4, 120), false,
                  /*warp_fade=*/true) == TransitionKind::kFadePair);
}

TEST_CASE("caves fade in both modes") {
    SystemsState in = at(1, 101, 40, 120);
    in.cave_flag = 1;
    for (const bool enhanced : {false, true}) {
        CHECK(kind_of(at(1, 1, 200, 120), in, enhanced) ==
              TransitionKind::kFadePair);
        CHECK(kind_of(in, at(1, 1, 200, 120), enhanced) ==
              TransitionKind::kFadePair);
    }
}

TEST_CASE("the secret room slides in enhanced and fades in classic") {
    SystemsState room = at(1, 100, 160, 120);
    room.secret_flag = 1;
    const SystemsState surface = at(1, 5, 160, 120);
    CHECK(kind_of(surface, room, true) == TransitionKind::kSecretEntry);
    CHECK(kind_of(room, surface, true) == TransitionKind::kSecretExit);
    CHECK(kind_of(surface, room, false) == TransitionKind::kFadePair);
    CHECK(kind_of(room, surface, false) == TransitionKind::kFadePair);
}

TEST_CASE("the L7 fake cave (12|13) fades in enhanced and pans in classic") {
    const SystemsState s12 = at(7, 12, 310, 130);
    const SystemsState s13 = at(7, 13, 48, 130);
    CHECK(kind_of(s12, s13, true) == TransitionKind::kFadePair);
    CHECK(kind_of(s12, s13, false) == TransitionKind::kPan);
    // A contiguous seam on the same level still pans.
    CHECK(kind_of(at(7, 13, 310, 130), at(7, 14, 4, 130), true) ==
          TransitionKind::kPan);
}

TEST_CASE("the L3 trunk descent is its own sequence, 17 -> 18 only") {
    CHECK(is_l3_trunk_descent(PrevFrame(at(3, 17, 150, 68)),
                              at(3, 18, 150, 10)));
    CHECK_FALSE(is_l3_trunk_descent(PrevFrame(at(3, 16, 310, 68)),
                                    at(3, 17, 4, 68)));
    CHECK_FALSE(is_l3_trunk_descent(PrevFrame(at(7, 17, 150, 68)),
                                    at(7, 18, 150, 10)));
}

TEST_CASE("take_warp_fade reads and clears the pending warp") {
    SystemsState st;
    CHECK_FALSE(take_warp_fade(st));
    st.player.cave_warp_pending = true;
    CHECK(take_warp_fade(st));
    CHECK_FALSE(st.player.cave_warp_pending);
    st.player.cave_warp_freeze = 0x3E8;
    CHECK(take_warp_fade(st));
}

TEST_CASE("ShownAs shows the frame before and puts the live state back") {
    SystemsState before = at(1, 3, 120, 90);
    before.cave_flag = 1;
    before.cave_index = 2;
    before.player.sprite = 46;
    before.cave_emerge_frames = 0;
    const PrevFrame pf(before);

    SystemsState now = at(1, 1, 200, 140);   // the exit moved everything on
    now.cave_index = -1;
    now.player.sprite = 3;
    now.cave_emerge_frames = 9;
    const ShownFields live = ShownFields::of(now);
    {
        const ShownAs shown(now, ShownFields::before(pf));
        CHECK(now.current_screen == 3);
        CHECK(now.player.x == 120);
        CHECK(now.cave_flag == 1);
        CHECK(now.cave_index == 2);
        CHECK(now.player.sprite == 46);
        CHECK(now.cave_emerge_frames == 0);
    }
    const ShownFields back = ShownFields::of(now);
    CHECK(back.screen == live.screen);
    CHECK(back.px == live.px);
    CHECK(back.cave_index == -1);
    CHECK(back.psprite == 3);
    CHECK(back.emerge == 9);

    // A surface side shows no cave index, whatever PrevFrame read.
    SystemsState surface = at(1, 1, 10, 10);
    surface.cave_index = 4;
    CHECK(ShownFields::before(PrevFrame(surface)).cave_index == -1);
}
