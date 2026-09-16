// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The built-in gameplay profiles — the ONE definition of each
// (docs/internal/specs/2026-09-13-profile-families-design.md).  A profile used
// to live in four hand-kept copies: builtin_profile's pins, apply_profile's
// dos clears, adopt_preset's hardcoded key list and the menu's apply_preset.
// A new pinned key had to be taught to all four, and missing one was silent.
//
// Lives in presentation/menu, not app/: the menu's apply_preset reads it, and
// app may include presentation but never the reverse (scripts/check_layers.sh).
// Header-only and SDL-free, so every target that compiles app/config.cpp or
// menu/settings_apply.cpp gets it with no new source file to register.
//
// Profiles come in FAMILIES of two: a classic member and an enhanced member.
// The menu's Classic/Enhanced switch and the first-run question move within
// the session's family, so a handheld's "Enhanced" is its own member, never
// the desktop's omniscale x4.  Every profile belongs to exactly one family.

#pragma once

#include <cstddef>
#include <iterator>
#include <string>

namespace olduvai::presentation {

enum class ProfileRole { Classic, Enhanced };

struct ProfilePin {
    const char* key;
    const char* value;
};

struct ProfileDef {
    const char* name;
    const char* family;
    ProfileRole role;
    const ProfilePin* pins;
    std::size_t pin_count;
};

// "enhanced" leads every row: the menu stages pins in table order, sessions
// drain in stage order, and the rebuild the display keys trigger must read
// the new master flag (see apply_preset).
//
// dos = byte-faithful.  These four used to be "clears" special-cased in
// apply_profile, because the dos map was empty and could not undo the
// enhanced-side keys a saved config carries.  As pins they do the same job.
inline constexpr ProfilePin kDosPins[] = {
    {"enhanced", "false"},
    {"enhance", ""},
    {"hd_profile", "native"},
    {"aspect", "keep"},
};
// hd = the full enhanced stack + widescreen peeks.  Audio is deliberately
// pinned by NO profile: the auto-pick chain (MT-32 ROMs -> soundfont -> OPL)
// chooses the best backend each machine has.
inline constexpr ProfilePin kHdPins[] = {
    {"enhanced", "true"},
    {"hd_profile", "omniscale"},
    {"render_scale", "4"},
    {"aspect", "widescreen"},
};
// hd-handheld = the TrimUI Smart Pro / Powkiddy A12 shipped values, measured
// on both devices: smooth x3 fits 1280x720 and 1024x600 (TRIMUI_TUNING.md
// Finding 2), on the discrete path with 2 sub-frames (SPIKE_BOSS_PERF.md).
inline constexpr ProfilePin kHdHandheldPins[] = {
    {"enhanced", "true"},
    {"hd_profile", "smooth"},
    {"render_scale", "3"},
    {"aspect", "widescreen"},
    {"smooth_subframes", "2"},
    {"smooth_vsync", "off"},
};

inline constexpr ProfileDef kProfiles[] = {
    {"dos", "desktop", ProfileRole::Classic, kDosPins, std::size(kDosPins)},
    {"hd", "desktop", ProfileRole::Enhanced, kHdPins, std::size(kHdPins)},
    // dos-handheld has dos's pins: it exists so a launcher can start a
    // handheld in Classic while the menu still knows its Enhanced member.
    {"dos-handheld", "handheld", ProfileRole::Classic, kDosPins,
     std::size(kDosPins)},
    {"hd-handheld", "handheld", ProfileRole::Enhanced, kHdHandheldPins,
     std::size(kHdHandheldPins)},
};

// A session with no profile at all behaves as this family.
inline constexpr const char* kDefaultFamily = "desktop";

inline const ProfileDef* find_profile(const std::string& name) {
    for (const auto& p : kProfiles)
        if (name == p.name) return &p;
    return nullptr;
}

inline const ProfileDef* family_member(const std::string& family,
                                       ProfileRole role) {
    for (const auto& p : kProfiles)
        if (family == p.family && p.role == role) return &p;
    return nullptr;
}

// The menu's Style row and the first-run question speak "dos" / "hd" — a
// ROLE, not a profile name.  Resolve it within a family: "hd" means the
// enhanced member, anything else the classic member.  An unknown or empty
// family resolves in kDefaultFamily, so a missing family is never fatal.
//
// Returned BY VALUE (five trivially-copied fields): a reference return trips
// GCC's -Wdangling-reference whenever a caller passes a temporary string —
// a false positive (it points into kProfiles), but fatal under -Werror.
inline ProfileDef resolve_preset(const std::string& family,
                                 const std::string& preset) {
    const ProfileRole role =
        preset == "hd" ? ProfileRole::Enhanced : ProfileRole::Classic;
    const ProfileDef* p = family_member(family, role);
    if (p == nullptr) p = family_member(kDefaultFamily, role);
    return p != nullptr ? *p : kProfiles[0];
}

// "dos, hd, ..." — for error messages.
inline std::string profile_names() {
    std::string out;
    for (const auto& p : kProfiles) {
        if (!out.empty()) out += ", ";
        out += p.name;
    }
    return out;
}

}  // namespace olduvai::presentation
