// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Build + validate the presentation GameOptions from resolved settings.
// The untested other half of the main() decomposition begun with parse_args /
// merge_config: the --enhance parse, the tuning-flag validation, the cross-
// field derivations (with real 0.9.2 regression history), and the field-by-
// field GameOptions assembly — lifted out of main() so they gain a doctest
// home (audit A2).
#pragma once

#include <string>
#include <vector>

#include "cli_args.hpp"                // CliArgs
#include "options_resolve.hpp"         // PlaySettings
#include "presentation/game_app.hpp"   // GameOptions (SDL-free)

namespace olduvai::app {

// Result of build_game_options — mirrors ParseOutcome (parse_args): the callee
// NEVER prints.  On a validation failure `ok` is false and the caller prints
// `error` to stderr then returns `exit_code`.  `warnings` are non-fatal notices
// (e.g. the widescreen-without-HD fallback) the caller prints to stderr on the
// success path.  (Every validation precedes every warning, so a failure never
// carries warnings — the caller can print warnings then check `ok`.)
struct BuildOutcome {
    bool ok = true;
    int exit_code = 0;
    std::string error;                    // fatal message, includes its own '\n'
    std::vector<std::string> warnings;    // non-fatal notices, in emission order
};

// Turn the resolved CliArgs + PlaySettings into a ready-to-run GameOptions.
// Pure data-in/data-out (no stdout/stderr, off every hot path and the trace):
// parses the --enhance list, validates the tuning flags (display-mode,
// transitions, aspect, hd-font, banner-fx, hd-profile), applies the cross-field
// derivations (transitions-classic / --trace force smooth-motion off; vga_scan
// implies vsync only for classic), and fills every GameOptions field including
// the persist closure and save_path.  `ps` is mutated (enhanced / hd_profile
// resolution) exactly as the inline block did.
BuildOutcome build_game_options(const CliArgs& args, PlaySettings& ps,
                                olduvai::presentation::GameOptions& out);

}  // namespace olduvai::app
