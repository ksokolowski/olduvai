// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// F5 in-game bug capture — non-interactive port of the Python reference's
// bug-report pipeline.
//
// Writes a self-contained report directory under the bug-report root
// (see bug_report_root() for how that root is resolved):
//   <root>/<YYYY-MM-DD_HHMMSS>_L<display>_S<screen>/
//     state.json               — machine-readable snapshot (mirrors the
//                                 Python schema so `op studio` can browse it)
//     report.md                — prefilled human skeleton
//     screenshot.png           — the current rendered frame
//     screenshot_collision.png — frame + collision debug overlay
//     screenshot_entities.png  — frame + entity debug overlay
//
// Unlike the Python path there is NO Tk dialog: olduvai writes every file
// non-interactively and prints the created path to stdout.  The Python
// dialog/subprocess machinery is deliberately
// skipped.

#pragma once

#include <string>
#include <vector>

#include "formats/mat.hpp"
#include "formats/pc1.hpp"
#include "presentation/game_render.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

// Write a complete bug-report directory for the current frame.
//
//   state            — the live systems state (player, entities, level flags)
//   base_frame       — the composed gameplay FrameBuffer for this frame
//                      (the clean screenshot.png source)
//   entity_sprites   — LxSPR.MAT sprites (for the entity-overlay shot)
//   display_level    — in-game level number (1..7)
//   internal_level   — internal level id (assets/function-name space)
//   overlay_scale    — HD render scale (1 classic, hd_scale otherwise)
//
// Returns the path of the written directory (relative to cwd), or an empty
// string on failure.  On success the path is also printed to stdout as
// "bug report: <path>".
// Optional user annotations from the F5 form (tag / reproducibility /
// free-text description).  Default-constructed = the pre-form behaviour
// (tag "f5", reproducibility "unknown", empty description prompt).
struct BugAnnotations {
    std::string tag;
    std::string reproducibility;
    std::string description;
    bool empty() const {
        return tag.empty() && reproducibility.empty() && description.empty();
    }
};

std::string write_bug_report(const systems::SystemsState& state,
                             const FrameBuffer& base_frame,
                             const std::vector<formats::Sprite>& entity_sprites,
                             int display_level, int internal_level,
                             int overlay_scale,
                             const BugAnnotations& ann = {},
                             bool has_presented = false);

// User-chosen bug-report root (play.json `bug_report_dir`, set by the app
// after config merge).  "~"-prefixed values expand to the home directory.
void set_bug_report_dir(const std::string& dir);

// Resolve the directory new reports are written under:
//   $OLDUVAI_BUG_DIR  >  set_bug_report_dir() value  >  <home>/olduvai/bug_reports
// (home = $HOME, or %USERPROFILE% on Windows).
std::string bug_report_root();

}  // namespace olduvai::presentation
