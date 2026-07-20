// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Loader for the declarative menu model (data/menus.json) into a MenuModel.
// olduvai's src/app/config.cpp parser is flat-only; menus.json is nested
// (objects + arrays), so this ships a small self-contained recursive-descent
// JSON parser.

#pragma once

#include <optional>
#include <string>

#include "presentation/menu.hpp"

namespace olduvai::presentation {

// Parse menus.json text into a MenuModel. Throws std::runtime_error on
// malformed JSON or a structurally invalid model.
MenuModel parse_menus_json(const std::string& text);

// Load + parse from a file path. Returns nullopt if the file can't be read;
// propagates parse errors as exceptions.
std::optional<MenuModel> load_menus(const std::string& path);

// Compiled-in copy of data/menus.json (generated at build time by
// cmake/embed_text.cmake).  Fallback when no on-disk model is found, so a
// lone binary still has its pause menu; an on-disk file always wins.
const char* embedded_menus_json();
std::optional<MenuModel> load_menus_embedded();

}  // namespace olduvai::presentation
