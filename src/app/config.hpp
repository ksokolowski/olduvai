// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Settings file + game profiles.  Settings live at
// ~/.config/olduvai/play.json (flat JSON object).  Precedence:
// built-in defaults < --profile < settings file < explicit CLI flags.
// Built-in profiles: "dos" (byte-faithful defaults) and "hd" (the full
// enhanced visual pipeline).  A malformed file warns and is ignored —
// a broken config must never block playing.

#pragma once

#include <map>
#include <string>

namespace olduvai::app {

// Flat string→string map (numbers/bools stored canonically as text).
using Config = std::map<std::string, std::string>;

Config builtin_profile(const std::string& name);   // {} when unknown

// Merge a builtin profile into cfg.  "dos" (byte-faithful) also CLEARS the
// enhanced-side keys a saved config may carry — the empty dos map cannot.
// Single source of truth for --profile, the first-run dialog, and the
// session adoption of the dialog's choice.
void apply_profile(Config& cfg, const std::string& name);
Config load_config_file();                          // {} when absent
bool save_config_file(const Config& c);
std::string config_path();

}  // namespace olduvai::app
