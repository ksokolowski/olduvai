// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The title menu's About screen (spec 2026-09-18): which build this is, what
// it was built for and with, who wrote it, and what it recreates.
//
// about_build() reads what the COMPILER knows — the same version and build id
// --version and the F5 report print, the target OS/CPU, the optional parts
// compiled in.  about_lines() turns that into menu rows; it is pure, so the
// test feeds it a fixed build.  The text is ASCII on purpose: the classic
// charset starts at 0x20 and HdText rasterises byte by byte, so a UTF-8
// letter would draw as two wrong glyphs in either mode.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "presentation/menu/menu.hpp"

namespace olduvai::presentation {

struct AboutBuild {
    std::string version;    // OLDUVAI_VERSION
    std::string build_id;   // olduvai::build_id()
    std::string os, arch;   // "Linux", "arm64"
    std::string sdl;        // "SDL 2.30.9" — the runtime library, from the caller
    std::vector<std::string> features;   // "MT-32", "MIDI out"
};

// Widest About row, in characters: the menu slab grows to fit its readouts
// (compute_menu_layout) up to the 320-wide frame less a 4 px margin a side,
// i.e. 312 px, of which 34 are the slab's own chrome; 278 px / 8 px, the
// classic glyph advance.
inline constexpr std::size_t kAboutChars = 34;
// Readout rows the `about` screen declares in menus.json.  A release build
// fills 10; the spare rows take a wrapped dirty-tree build id.
inline constexpr std::size_t kAboutRows = 12;

AboutBuild about_build(const std::string& sdl_version);
std::vector<std::string> about_lines(const AboutBuild& b, std::size_t max_chars);

// Put `lines` into the `about` screen's readout rows, in order, and drop the
// readouts left over so the slab is no taller than its text.
void fill_about_screen(MenuModel& model, const std::vector<std::string>& lines);

}  // namespace olduvai::presentation
