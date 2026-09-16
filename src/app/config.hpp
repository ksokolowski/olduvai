// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Settings file + game profiles.  Settings live at
// ~/.config/olduvai/play.json (flat JSON object).  Precedence:
// built-in defaults < --default-profile < settings file < --profile <
// explicit CLI flags (options_resolve.hpp, layer_config).  The built-in
// profiles and their families: presentation/menu/profile_table.hpp.  A
// malformed file warns and is ignored — a broken config must never block
// playing.

#pragma once

#include <map>
#include <string>

namespace olduvai::app {

// Flat string→string map (numbers/bools stored canonically as text).
using Config = std::map<std::string, std::string>;

Config builtin_profile(const std::string& name);   // {} when unknown

// Merge a builtin profile's pins into cfg.  dos's pins include the
// enhanced-side resets (enhance, hd_profile, aspect) a saved config may
// carry.  Single source of truth for --profile, the first-run dialog, and
// the session adoption of the dialog's choice.
void apply_profile(Config& cfg, const std::string& name);
Config load_config_file();                          // {} when absent
bool save_config_file(const Config& c);
std::string config_path();

}  // namespace olduvai::app
