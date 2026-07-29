// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// What is left of the per-feature enhanced-mode toggles.
//
// There were seven: smooth_motion, cinematic_cue, hud_overlay, fluid_bubbles,
// secret_slide, descent_pan and hd_text.  Seven booleans are 128 combinations,
// of which the test corpus covered exactly two — all-off (the DOS default) and
// all-on (`--enhanced`).  The other 126 shipped untested, and at least one of
// them was visibly broken: with HD text off and widescreen on, the pause menu
// was drawn through a routine that assumed a 320-wide buffer, shearing it
// across the screen.  No golden caught it because every golden runs all-on.
//
// So `--enhanced` is now all-or-nothing and the six aesthetic flags are gone.
// smooth_motion stays, and NOT as a user-facing feature toggle: it is the one
// with machinery behind it.  --transitions classic turns it off, and --trace
// MUST turn it off, because tracing needs exactly one presented state per
// logic frame while sub-frame interpolation emits a display-refresh-dependent
// number of them.  It is resolved in options_build, not chosen from a menu.

#pragma once

namespace olduvai::presentation {

struct EnhanceFlags {
    bool smooth_motion = false;   // sub-frame motion interpolation
};

}  // namespace olduvai::presentation
