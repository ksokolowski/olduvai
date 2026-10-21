// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Timedemo: OLDUVAI_UNTHROTTLED=1 with --replay.  Every pacing wait returns
// at once (the PIT tick, smooth-motion sub-frames, transition steps, the
// sequence screens' holds) and wall-clock progress steps by frames instead,
// so a replay does a fixed amount of work as fast as the machine goes and
// frame-stats' eff_hz reads its headroom above 18.2 Hz.  The logic is
// tick-indexed, so the trace is the throttled run's.  window_util.cpp.
#pragma once

#include <cstdint>

namespace olduvai::presentation {

void set_unthrottled(bool on);
bool unthrottled();
// SDL_Delay(ms), or nothing when unthrottled.
void pace_delay(std::uint32_t ms);

}  // namespace olduvai::presentation
