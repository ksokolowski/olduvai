// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The one enhanced-mode flag resolved apart from `--enhanced`, which is
// all-or-nothing (per-feature toggles multiplied the combinations no test ran).
// smooth_motion is resolved in options_build, not chosen from a menu:
// --transitions classic turns it off, and --trace must, because a trace needs
// exactly one presented state per logic frame while sub-frame interpolation
// emits a refresh-dependent number of them.

#pragma once

namespace olduvai::presentation {

struct EnhanceFlags {
    bool smooth_motion = false;   // sub-frame motion interpolation
};

}  // namespace olduvai::presentation
