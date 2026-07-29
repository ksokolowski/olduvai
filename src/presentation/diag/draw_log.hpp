// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// OLDUVAI_DRAW_LOG debug aid: one JSONL line per rendered (sub)frame with the
// player position/sprite and every active entity [type,sprite,x,y,visible].
// Analysed by the reference repo's scripts/analyze_draw_log.py (catches lerped
// teleports and one-frame sprite blips).  No-op when the FILE* is null.
#pragma once

#include <cstdio>

#include "systems/player.hpp"   // systems::SystemsState

namespace olduvai::presentation {

void write_draw_log(std::FILE* f, int frame, int sub,
                    const systems::SystemsState& s);

}  // namespace olduvai::presentation
