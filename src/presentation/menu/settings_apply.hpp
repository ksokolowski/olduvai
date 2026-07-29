// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure (no-SDL) classification of an Options change into how it must be
// applied at runtime — Live (in-place), Reinit (window/audio rebuild via the
// save→reinit→restore path), or PersistOnly (write config, applies next
// launch).
#pragma once

#include "presentation/menu/menu.hpp"
#include "presentation/menu/settings_session.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace olduvai::presentation {

// Mirrors the engine's HD sizing rule (game_app.cpp): HD is active only when
// enhanced AND the profile is not "native"; scale caps at 4, else 2, else 1.
// Is the HD substrate active?  `enhanced` alone is not enough — hd_profile
// "native" means enhanced mode without upscaling, and every site that gates
// on HD has to test both.  Both drivers open by re-deriving this expression
// by hand; hd_scale_for already computed it internally and discarded it.
bool hd_active(bool enhanced, const std::string& hd_profile);

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
// "enhanced" is set FIRST: sessions drain in stage order and the rebuild
// triggered by the hd keys must read the new master flag.
void apply_preset(MenuBindings& bind, const std::string& preset);


}  // namespace olduvai::presentation
