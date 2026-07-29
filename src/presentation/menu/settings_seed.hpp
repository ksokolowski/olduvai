// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Baseline seeding of an Options bindings instance (`cur` + the mem map)
// from the current runtime values.  The same ~17 keys were seeded verbatim
// at the three Options environments (surface pause, title menu, boss
// pause); drifting copies is exactly how a new setting ends up showing a
// stale value in one menu only (CC3 phase 4, slice 2).
//
// SDL-free on purpose: the fullscreen flag arrives as a bool (the caller
// reads the SDL window), so this seeds in the headless-testable tier.
// Templated over the bind type because each site's bindings struct is its
// own MenuBindings subclass — they share the `mem`/`cur` field shape, not
// a base class (yet; unifying set()/get() is a later slice).

#pragma once

#include <string>

#include "presentation/enhance_flags.hpp"
#include "presentation/menu/settings_apply.hpp"  // DisplaySettings

namespace olduvai::presentation {

// Snapshot of the runtime values the Options screens open onto.
struct SettingsSeed {
    bool enhanced = false;
    std::string hd_profile;       // "" reads as "native"
    int render_scale = 0;
    std::string music_device;
    std::string sfx_backend;
    std::string aspect;           // "" reads as "keep"
    bool fullscreen = false;      // caller reads the SDL window flag
    EnhanceFlags flags;
};

template <class Bind>
void seed_settings_mem(Bind& b, const SettingsSeed& s) {
    b.cur = {s.enhanced,
             s.hd_profile.empty() ? "native" : s.hd_profile,
             s.render_scale, s.music_device, s.sfx_backend};
    b.mem["music_device"] = s.music_device;
    b.mem["sfx_backend"] = s.sfx_backend;
    b.mem["hd_profile"] = s.hd_profile.empty() ? "native" : s.hd_profile;
    b.mem["render_scale"] = std::to_string(s.render_scale);
    b.mem["aspect"] = s.aspect.empty() ? "keep" : s.aspect;
    // Master-flag baseline: lets a preset click that matches the current
    // style net out of the staging diff (and marks the master as genuinely
    // staged when it does change).
    b.mem["enhanced"] = s.enhanced ? "true" : "false";
    // Preset row display seed — derived, not stored: exact HD shapes label
    // as such, anything else shows Classic.
    b.mem["preset"] =
        !s.enhanced ? "dos" : "hd";
    b.mem["fullscreen"] = s.fullscreen ? "1" : "0";
    b.mem["music_volume"] = "100";
    b.mem["sfx_volume"] = "100";
}

}  // namespace olduvai::presentation
