// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The built-in gameplay profiles, one definition each.  In presentation/menu
// because the menu reads it and app may include presentation, not the reverse
// (check_layers.sh).  Header-only, SDL-free.  Profiles come in families of two
// (classic + enhanced); the Classic/Enhanced switch and the first-run question
// move within the session's family, so a handheld's Enhanced is its own
// member.

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

// "enhanced" leads every row: pins stage in table order, and the rebuild the
// display keys trigger must read the new master flag.
// dos = byte-faithful; these pins also undo the enhanced keys a saved config
// carries.
inline constexpr ProfilePin kDosPins[] = {
    {"enhanced", "false"},
    {"enhance", ""},
    {"hd_profile", "native"},
    {"aspect", "keep"},
};
// hd = the full enhanced stack + widescreen peeks.  No profile pins audio: the
// auto chain (MT-32 ROMs -> SoundFont -> OPL) picks per machine.
inline constexpr ProfilePin kHdPins[] = {
    {"enhanced", "true"},
    {"hd_profile", "omniscale"},
    {"render_scale", "4"},
    {"aspect", "widescreen"},
};
// hd-handheld: measured on the TrimUI Smart Pro and Powkiddy A12: smooth x3
// fits 1280x720 and 1024x600, discrete path with 2 sub-frames.
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

// The button layout a family's devices print, as a DEVICE default: set only
// by a launcher's --default-profile, beneath play.json, and never by the
// Style switch (which would reset a player's own mapping).  nullptr: the
// engine default (xbox).
inline const char* family_button_layout(const std::string& family) {
    return family == "handheld" ? "nintendo" : nullptr;
}

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

// The Style row and first-run question speak a role ("dos" / "hd"), resolved
// within a family ("hd" = the enhanced member); an unknown or empty family
// uses kDefaultFamily.  By value: a reference return trips GCC's
// -Wdangling-reference on temporary arguments (-Werror).
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
