// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Pure (no-SDL) classification of an Options change: Live (in place), Reinit
// (window/audio rebuild through save -> reinit -> restore) or PersistOnly
// (saved, applies next launch).
#pragma once

#include "presentation/menu/menu.hpp"
#include "presentation/menu/settings_session.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace olduvai::presentation {

// HD is active when enhanced and the profile is not "native" (enhanced
// without upscaling); every HD gate must test both.
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

// The pipeline keys of the runtime options, verbatim ("" hd_profile stays
// "").  A template so the menu layer needs no GameOptions include.
template <class Opts>
DisplaySettings display_settings_of(const Opts& o) {
    return {o.enhanced, o.hd_profile, o.render_scale, o.music_device,
            o.sfx_backend};
}

// Write one pipeline key (enhanced, render_scale, hd_profile, music_device,
// sfx_backend) into `d`.  A malformed render_scale keeps the current value.
// False for any other key.
bool set_display_key(DisplaySettings& d, const std::string& key,
                     const std::string& value);

// How one Options change must be applied, given the live settings.  Audio and
// video keys only; enhance.* and cheat.* have their own paths.
ApplyTier classify_change(const std::string& key, const std::string& new_value,
                          const DisplaySettings& cur);

// Set-aware: staged display keys act together.  A preset flipping `enhanced`
// and `hd_profile` crosses the classic <-> HD scale boundary though neither key
// does alone; per-key classification would call it PersistOnly and Apply would
// do nothing.  If the combined target changes hd_scale_for, every display key
// in `staged` is Reinit; otherwise classify_change.
ApplyTier classify_change_in_set(
    const std::string& key, const std::string& new_value,
    const DisplaySettings& cur,
    const std::vector<std::pair<std::string, std::string>>& staged);



// Style preset (the GUI --profile): fan the family member for `preset` ("dos"
// / "hd", profile_table.hpp) through MenuBindings::set, so every key stages,
// previews and applies normally.  Classic stages only `enhanced` and
// `aspect`.  `enhanced` first: the rebuild must read the new master flag.
void apply_preset(MenuBindings& bind, const std::string& preset);


}  // namespace olduvai::presentation
