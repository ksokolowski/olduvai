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
// hd = the full enhanced stack + widescreen peeks.  Audio is the auto chain
// (MT-32 ROMs -> SoundFont -> OPL), picked per machine.
inline constexpr ProfilePin kHdPins[] = {
    {"enhanced", "true"},
    {"hd_profile", "omniscale"},
    {"render_scale", "4"},
    {"aspect", "widescreen"},
};
// The handheld pair follows the panel the session runs on: "auto" pins are
// resolved by auto_display() against session_panel() (set at start-up).
// Enhanced is measured on the TrimUI Smart Pro and Powkiddy A12 (x3, 1280x720
// and 1024x600) and the R36S (x2 filling 640x480), discrete path with 2
// sub-frames.  xBRZ is the scaler: the same steady frame cost as Scale2x/3x,
// 0.15 ms a tick more, and a worse burst when a screen's graphics are built.
// Audio: auto without the GM SoundFont (FluidSynth is a real cost on a weak
// chip, and the Sound Blaster is preferred to it): MT-32, then OPL.
inline constexpr ProfilePin kDosHandheldPins[] = {
    {"enhanced", "false"},
    {"enhance", ""},
    {"hd_profile", "native"},
    {"aspect", "auto"},
    {"music_auto_gm", "false"},
};
inline constexpr ProfilePin kHdHandheldPins[] = {
    {"enhanced", "true"},
    {"hd_profile", "xbrz"},
    {"render_scale", "auto"},
    {"aspect", "auto"},
    {"smooth_subframes", "2"},
    {"smooth_vsync", "off"},
    {"music_auto_gm", "false"},
};

inline constexpr ProfileDef kProfiles[] = {
    {"dos", "desktop", ProfileRole::Classic, kDosPins, std::size(kDosPins)},
    {"hd", "desktop", ProfileRole::Enhanced, kHdPins, std::size(kHdPins)},
    {"dos-handheld", "handheld", ProfileRole::Classic, kDosHandheldPins,
     std::size(kDosHandheldPins)},
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
    return family.rfind("handheld", 0) == 0 ? "nintendo" : nullptr;
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

// The panel a handheld session runs on, for "auto" pins; 0x0 = unknown.
struct Panel {
    int w = 0, h = 0;
};
inline Panel& session_panel() {
    static Panel p;
    return p;
}

struct AutoDisplay {
    int render_scale;
    const char* aspect;
};

// "auto" for `panel`: a panel narrower than 16:10 is filled at 4:3, a wider
// one is widescreen in Enhanced (keep in Classic); the scale is the largest,
// 2 to 4, whose frame (320 wide, 240 or 200 tall) fits the panel.  An unknown
// panel plays the measured 720p default, x3 widescreen.
inline AutoDisplay auto_display(const Panel& panel, ProfileRole role) {
    const bool wide_role = role == ProfileRole::Enhanced;
    if (panel.w <= 0 || panel.h <= 0)
        return {3, wide_role ? "widescreen" : "keep"};
    const bool narrow = panel.w * 10 < panel.h * 16;
    const int by_h = panel.h / (narrow ? 240 : 200);
    const int by_w = panel.w / 320;
    const int s = by_h < by_w ? by_h : by_w;
    return {s < 2 ? 2 : (s > 4 ? 4 : s),
            narrow ? "4:3" : (wide_role ? "widescreen" : "keep")};
}

// A pin's value, "auto" resolved for the session's panel.
inline std::string pin_value(const ProfileDef& p, std::size_t i) {
    const std::string v = p.pins[i].value;
    if (v != "auto") return v;
    const AutoDisplay a = auto_display(session_panel(), p.role);
    return std::string(p.pins[i].key) == "render_scale"
               ? std::to_string(a.render_scale)
               : std::string(a.aspect);
}

}  // namespace olduvai::presentation
