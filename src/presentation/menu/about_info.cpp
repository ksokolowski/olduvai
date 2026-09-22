// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "presentation/menu/about_info.hpp"

#include <algorithm>
#include <sstream>

#include "core/build_id.hpp"

#ifndef OLDUVAI_VERSION
#define OLDUVAI_VERSION "0.0.0"
#endif

namespace olduvai::presentation {

namespace {

// Greedy word wrap; a word longer than the row is cut.
void wrap_into(std::vector<std::string>& out, const std::string& text,
               std::size_t max_chars) {
    std::istringstream words(text);
    std::string word, line;
    while (words >> word) {
        while (word.size() > max_chars) {
            if (!line.empty()) { out.push_back(line); line.clear(); }
            out.push_back(word.substr(0, max_chars));
            word.erase(0, max_chars);
        }
        if (word.empty()) continue;
        if (line.empty()) line = word;
        else if (line.size() + 1 + word.size() <= max_chars) line += " " + word;
        else { out.push_back(line); line = word; }
    }
    if (!line.empty()) out.push_back(line);
}

}  // namespace

AboutBuild about_build(const std::string& sdl_version) {
    AboutBuild b;
    b.version = OLDUVAI_VERSION;
    b.build_id = olduvai::build_id();
#if defined(__APPLE__)
    b.os = "macOS";
#elif defined(_WIN32)
    b.os = "Windows";
#elif defined(__linux__)
    b.os = "Linux";
#else
    b.os = "unknown OS";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    b.arch = "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    b.arch = "armhf";
#elif defined(__x86_64__) || defined(_M_X64)
    b.arch = "x86-64";
#elif defined(__i386__) || defined(_M_IX86)
    b.arch = "x86";
#endif
    b.sdl = sdl_version;
#ifdef OLDUVAI_VENDORED_MT32EMU
    b.features.emplace_back("MT-32");
#endif
#ifdef OLDUVAI_HAVE_RTMIDI
    b.features.emplace_back("MIDI out");
#endif
    return b;
}

std::vector<std::string> about_lines(const AboutBuild& b,
                                     std::size_t max_chars) {
    std::vector<std::string> out;
    wrap_into(out, "Olduvai " + b.version, max_chars);
    wrap_into(out, "Build " + b.build_id, max_chars);
    std::string platform = b.os;
    if (!b.arch.empty()) platform += " " + b.arch;
    if (!b.sdl.empty()) platform += ", " + b.sdl;
    wrap_into(out, platform, max_chars);
    if (!b.features.empty()) {
        std::string f = "Features:";
        for (std::size_t i = 0; i < b.features.size(); ++i)
            f += (i ? ", " : " ") + b.features[i];
        wrap_into(out, f, max_chars);
    }
    wrap_into(out, "(c) 2026 Krzysztof Sokolowski", max_chars);
    wrap_into(out, "GPL-3.0-or-later", max_chars);
    wrap_into(out, "github.com/ksokolowski/olduvai", max_chars);
    wrap_into(out,
              "A native engine recreation of Prehistorik (1991, Titus).",
              max_chars);
    wrap_into(out, "Requires your own game files.", max_chars);
    if (out.size() > kAboutRows) out.resize(kAboutRows);
    return out;
}

void fill_about_screen(MenuModel& model, const std::vector<std::string>& lines) {
    const auto it = model.screens.find("about");
    if (it == model.screens.end()) return;
    auto& items = it->second.items;
    std::size_t next = 0;
    for (MenuItem& row : items)
        if (row.type == "readout" && next < lines.size())
            row.label = lines[next++];
    // The readouts still empty are the surplus.
    items.erase(std::remove_if(items.begin(), items.end(),
                               [](const MenuItem& row) {
                                   return row.type == "readout" &&
                                          row.label.empty();
                               }),
                items.end());
}

}  // namespace olduvai::presentation
