// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Full-state save capture / restore — moved verbatim from game_app.cpp (CC2b).
// See level_save.hpp.

#include "presentation/level_save.hpp"

#include <algorithm>
#include <vector>

#include "core/rng.hpp"
#include "core/types.hpp"
#include "presentation/level_setup.hpp"  // bind_screen (called by apply_save)
#include "presentation/save_state.hpp"

namespace olduvai::presentation {

// ── Full-state save capture / restore (operate on the whole Loaded g) ──────
SaveState capture_save(const Loaded& g, int display_level) {
    SaveState sv;
    sv.hdr = capture_header(g.state, display_level);
    sv.hdr.rng = core::global_rng().state();
    for (const auto& e : g.state.entities)
        sv.entities.push_back(snapshot_entity(e));   // current (live) screen
    for (const auto& [k, list] : g.store) {           // every visited screen
        ScreenEntities se;
        se.key = k;
        for (const auto& e : list) se.entities.push_back(snapshot_entity(e));
        sv.store.push_back(std::move(se));
    }
    sv.bound_key = g.bound_key;
    return sv;
}

void overlay_list(const std::vector<EntitySnapshot>& snaps,
                  std::vector<core::Entity>& ents) {
    const std::size_t n = std::min(snaps.size(), ents.size());
    for (std::size_t i = 0; i < n; ++i)
        if (static_cast<int>(ents[i].obj_type) == snaps[i].obj_type)
            overlay_entity(snaps[i], ents[i]);  // index-matched (deterministic spawn)
}

// Restore onto a freshly-loaded level: set scalars+player, overlay the stored
// screens, re-bind the EXACT screen/mode, overlay the live list.  Entities come
// from a fresh spawn (correct derived data) + the saved runtime overlay.
void apply_save(const SaveState& sv, Loaded& g) {
    apply_header(sv.hdr, g.state);
    core::global_rng().reseed(sv.hdr.rng);
    // load_level left its default screen (0) BOUND — bind_store moved that
    // slot's freshly-spawned list into the live `g.state.entities`, leaving
    // g.store[0] empty (moved-from).  Flush it back so EVERY screen — including
    // the one we are about to re-bind — has its fresh-spawn list present for the
    // overlay below.  Without this, restoring onto the load_level default screen
    // re-binds an EMPTY list and `overlay_list(sv.entities, …)` no-ops (min size
    // 0), so every entity on that screen is lost (the L1 screen-0 spike vanished
    // after an in-game reinit — only screen 0 broke because only its slot was
    // emptied; caves/secrets/other screens kept theirs).  Regression-guarded by
    // tests/reinit_smoke.sh (entity-count round-trip).
    if (g.bound_key >= 0) {
        g.store[g.bound_key] = std::move(g.state.entities);
        g.bound_key = -1;
    }
    for (const auto& se : sv.store) {
        auto it = g.store.find(se.key);
        if (it != g.store.end()) overlay_list(se.entities, it->second);
    }
    g.bound_key = -1;   // stop bind_store from clobbering the overlaid store
    bind_screen(g, g.state.current_screen);   // render + fresh live entities + mode
    overlay_list(sv.entities, g.state.entities);
}

}  // namespace olduvai::presentation
