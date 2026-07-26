// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/draw_log.hpp"

namespace olduvai::presentation {

void write_draw_log(std::FILE* f, int frame, int sub,
                    const systems::SystemsState& s) {
    if (f == nullptr) return;
    std::fprintf(f,
                 "{\"f\":%d,\"sub\":%d,\"px\":%d,\"py\":%d,\"ps\":%d,\"e\":[",
                 frame, sub, s.player.x, s.player.y, s.player.sprite);
    bool first = true;
    for (const auto& e : s.entities) {
        if (!e.active) continue;
        std::fprintf(f, "%s[%d,%d,%d,%d,%d]", first ? "" : ",",
                     static_cast<int>(e.obj_type), e.sprite, e.x, e.y,
                     e.visible ? 1 : 0);
        first = false;
    }
    std::fprintf(f, "]}\n");
}

}  // namespace olduvai::presentation
