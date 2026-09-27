// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// F5 in-game bug capture (non-interactive).  Writes a report directory under
// bug_report_root():
//   <root>/<YYYY-MM-DD_HHMMSS>_L<display>_S<screen>/
//     state.json               machine-readable snapshot (reference schema)
//     report.md                prefilled skeleton
//     screenshot.png           the rendered frame
//     screenshot_collision.png frame + collision overlay
//     screenshot_entities.png  frame + entity overlay
// and prints the created path.

#pragma once

// Forward declarations, not <SDL.h>: main.cpp includes this header, and on
// Windows SDL.h renames main() to SDL_main (then needing SDL2main to link).
struct SDL_Renderer;
struct SDL_Window;

#include <functional>
#include <string>
#include <vector>

#include "formats/mat.hpp"
#include "formats/pc1.hpp"
#include "presentation/render/game_render.hpp"
#include "systems/player.hpp"

namespace olduvai::presentation {

// Optional F5-form annotations (tag, reproducibility, description).  Default:
// tag "f5", reproducibility "unknown", empty description.
struct BugAnnotations {
    std::string tag;
    std::string reproducibility;
    std::string description;
    bool empty() const {
        return tag.empty() && reproducibility.empty() && description.empty();
    }
};

// Live present-path state for the report's Display section, so a display
// defect's report carries the display state.  Default = not supplied (the
// section is omitted).
struct DisplayInfo {
    bool supplied = false;
    int out_w = 0, out_h = 0;        // SDL_GetRendererOutputSize
    int logical_w = 0, logical_h = 0;  // 0,0 = no logical size set
    bool fullscreen = false;
    bool hd = false;
    int hd_scale = 1;
    // The Aspect setting verbatim (keep / widescreen / 4:3 / stretch).  The
    // margin is computed only for "widescreen", so margin 0 alone cannot tell
    // "display too narrow" from "widescreen not selected".
    std::string aspect;
    bool ws_active = false;          // widescreen actually composing
    int ws_margin = 0;               // derived peek margin, 0 = inactive
    int ws_native_w = 320;           // 320 + 2*margin
    int upscale_threads = 1;
};

// The generic part of DisplayInfo, read from the renderer and window (output
// and logical size, fullscreen, upscale threads).  Drivers add HD and
// widescreen fields.
DisplayInfo read_display_info(SDL_Renderer* ren, SDL_Window* win);

// A boss fight's state for a report from the arena.  The SystemsState passed
// with it is then synthesised (position, lives, score, level) and
// platform-only rows read n/a.  Default = a platform report.
struct BossInfo {
    bool supplied = false;
    int health = 0;        // counts down; the fight is won at 272
    std::string phase;     // the boss's own state, one line
    int frame = 0;         // fight frame
};

// What a bug report records: the moment's state and its clean frame, the
// level, the player's notes, and how the frame was shown.
struct BugReport {
    const systems::SystemsState& state;
    const FrameBuffer& frame;   // the clean scene (screenshot.png)
    const std::vector<formats::Sprite>& entity_sprites;
    int display_level = 0;
    int internal_level = 0;
    BugAnnotations ann;
    // A screenshot_presented.png joins it (HD or widescreen): the frame as
    // the player saw it.
    bool has_presented = false;
    DisplayInfo display;
    BossInfo boss;   // default: a platform report
};

// The report's directory, or "" when none could be created.
std::string write_bug_report(const BugReport& r);

// write_bug_report, then — when the report has a presented frame — `redraw`
// draws the frame as shown without presenting, and the renderer's output is
// saved beside it (a post-present readback is black on Metal).
void write_bug_report_as_shown(const BugReport& r, SDL_Renderer* ren,
                               const std::function<void()>& redraw);

// Bug-report root from play.json `bug_report_dir`; "~" expands to home.
void set_bug_report_dir(const std::string& dir);

// Where new reports go:
//   $OLDUVAI_BUG_DIR > set_bug_report_dir() > <home>/olduvai/bug_reports
// (home = $HOME, or %USERPROFILE% on Windows).
std::string bug_report_root();

}  // namespace olduvai::presentation
