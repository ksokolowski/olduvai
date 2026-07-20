// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure (no-SDL) classification of an Options change into how it must be
// applied at runtime — Live (in-place), Reinit (window/audio rebuild via the
// save→reinit→restore path), or PersistOnly (write config, applies next
// launch).
#pragma once

#include "presentation/menu.hpp"
#include "presentation/settings_session.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace olduvai::presentation {

// Mirrors the engine's HD sizing rule (game_app.cpp): HD is active only when
// enhanced AND the profile is not "native"; scale caps at 4, else 2, else 1.
int hd_scale_for(bool enhanced, const std::string& hd_profile, int render_scale);

enum class ApplyTier { Live, Reinit, PersistOnly };

struct DisplaySettings {
    bool enhanced = false;
    std::string hd_profile = "native";
    int render_scale = 2;
    std::string music_device = "auto";
    std::string sfx_backend = "auto";
};

// Decide how a single Options change (one key) must be applied, given the
// current live settings.  `key` is a menu/config key; `new_value` is the
// stringified target.  Only the audio/video keys are classified here; enhance.*
// and cheat.* are handled by their own code paths.
ApplyTier classify_change(const std::string& key, const std::string& new_value,
                          const DisplaySettings& cur);

// Set-aware classification: the staged display keys act TOGETHER.  A preset
// that flips `enhanced` AND `hd_profile` crosses the classic<->HD scale
// boundary even though neither key does alone against the same baseline —
// per-key classification would call the whole set PersistOnly and the Apply
// would visibly do nothing (the first-run Style->Enhanced report).  `staged`
// is every (key, new_value) pair currently staged in the session; when the
// jointly-overlaid target changes hd_scale_for, every display key in the set
// classifies Reinit.  Non-display keys and non-boundary sets fall back to
// classify_change.
ApplyTier classify_change_in_set(
    const std::string& key, const std::string& new_value,
    const DisplaySettings& cur,
    const std::vector<std::pair<std::string, std::string>>& staged);



// One-click presentation preset (the GUI equivalent of --profile): fans a
// named bundle out through MenuBindings::set so every key rides the normal
// staging/preview/confirm/apply machinery of the calling menu environment.
//   dos    — classic: master flag off, every cave-painting flag off, aspect keep
//   hd     — full enhanced: omniscale x4, widescreen, all flags on
//   hd-43  — full enhanced at the classic 4:3 aspect
// "enhanced" is set FIRST: sessions drain in stage order and the rebuild
// triggered by the hd keys must read the new master flag.
void apply_preset(MenuBindings& bind, const std::string& preset);

// Config writes for one enhance.* key draining through an Apply — the shared
// granular-persist encoding of the three Options sites (pause / title / boss).
// enhance.* keys stage like every other editable key, but play.json stores
// them as ONE comma list under "enhance" (menu enhance.foo_bar → config token
// foo-bar), derived from `mem` in a single shot.  So:
//   * only the FIRST enhance.* key of the staged set flushes (later keys
//     return no writes — the list already covered them);
//   * when the master "enhanced" flag is NOT part of the same apply, the
//     flush appends "enhanced=false": a granular edit converts an all-on
//     bundle config into the explicit list (a stale enhanced=true would
//     re-enable every flag at load);
//   * when the master IS staged, its own persisted value must stand —
//     clobbering it with the companion was the Style-preset Discard bug.
std::vector<std::pair<std::string, std::string>> encode_enhance_persist(
    const std::map<std::string, std::string>& mem,
    const std::vector<StagedChange>& staged, const std::string& draining_key);

}  // namespace olduvai::presentation
