// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// A screen change's decisions, apart from its pixels: the frame before
// (PrevFrame), which transition plays, and the state as a frame showed it.
// SDL-free; test_transition_classify.cpp.
#pragma once

#include "presentation/sequence/transition_geometry.hpp"   // TransitionKind
#include "systems/player.hpp"

namespace olduvai::presentation {

// The frame's BEFORE picture: state at the top of this frame, read by the
// transition classifier, the screen change and the level-complete fade.
struct PrevFrame {
    explicit PrevFrame(const systems::SystemsState& s)
        : px(s.player.x), py(s.player.y), screen(s.current_screen),
          secret(s.secret_flag != 0), cave(s.cave_flag != 0),
          cave_index(s.cave_index), inside(s.cave_flag || s.secret_flag),
          psprite(s.player.sprite), pdx(s.player.dx), pdy(s.player.dy),
          pfacing(s.player.facing_left), pclub(s.player.club_flag),
          emerge(s.cave_emerge_frames) {}

    // Position before this frame's movement/teleport (direction inference).
    int px, py;
    int screen;
    bool secret, cave;
    // cave_index before this frame's logic (a cave-sign exit sets it to -1).
    // The wide fade re-composes the outgoing cave frame, and the STOP-sign
    // render is gated on cave_index being in range.
    int cave_index;
    bool inside;
    // Player draw state before this frame's logic = the last presented frame.
    // The wide fade re-composes its outgoing frame, but the transition tick
    // has already changed it: cave entry resets sprite/dx/facing/club (the
    // frame would show a standing player instead of descent frame 46); cave
    // exit arms cave_emerge_frames (the PLAYER_TURN override would leak in).
    // Locals only: PlayerState is memcpy'd into the POD SaveHeader, so no
    // shadow fields may live there.
    int psprite, pdx, pdy, pfacing, pclub, emerge;
};

struct TransitionChoice {
    TransitionKind kind = TransitionKind::kNone;
    char dir = 'R';   // kPan only: 'R' / 'L' / 'D' / 'U'
};

// The transition a screen change plays.  `warp_fade`: a cave warp this frame
// (take_warp_fade).  The L7 fake-cave seam fades in enhanced; the EXE warps
// instantly (25b2:07df skips the wipe) and classic pans, as the reference
// (PARITY T4).  The L3 trunk descent is not a transition (is_l3_trunk_descent).
TransitionChoice classify_transition(const PrevFrame& pf,
                                     const systems::SystemsState& st,
                                     bool enhanced, bool warp_fade);

// A cave warp plays the fade pair.  cave_warp_pending is a per-frame
// transient: read and cleared here.
bool take_warp_fade(systems::SystemsState& st);

// L3 17 -> 18: the trunk-descent cinematic instead of a transition.
bool is_l3_trunk_descent(const PrevFrame& pf, const systems::SystemsState& st);

// The state a transition's outgoing frame reads that the screen change has
// already moved on: the player's position and pose, the screen, cave, secret.
struct ShownFields {
    int px = 0, py = 0, screen = 0, cave = 0, cave_index = -1, secret = 0;
    int psprite = 0, pdx = 0, pdy = 0, pfacing = 0, pclub = 0, emerge = 0;

    static ShownFields of(const systems::SystemsState& st);
    // As the frame before showed them; the cave index only on a cave side.
    static ShownFields before(const PrevFrame& pf);
    void apply(systems::SystemsState& st) const;
};

// Draw the state as `shown` for a scope; the live values come back at its end.
// Composes inside it must not advance state (a player-less compose, or
// advance_state = false): the restore would undo the advance.
class ShownAs {
public:
    ShownAs(systems::SystemsState& st, const ShownFields& shown)
        : st_(st), live_(ShownFields::of(st)) {
        shown.apply(st_);
    }
    ~ShownAs() { live_.apply(st_); }
    ShownAs(const ShownAs&) = delete;
    ShownAs& operator=(const ShownAs&) = delete;

private:
    systems::SystemsState& st_;
    ShownFields live_;
};

}  // namespace olduvai::presentation
