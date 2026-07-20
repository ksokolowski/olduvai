// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// M2 asset viewer — the prepare-pipeline debug tool and the engine's first
// pixels on screen.  Browses decoded PC1 backgrounds and MAT sprites
// straight from the user's archives.
//
// Keys: Left/Right = item · Up/Down = MAT file · Tab = PC1/MAT mode ·
//       Esc/Q = quit.

#pragma once

#include <filesystem>
#include <string>

namespace olduvai::presentation {

struct ViewerOptions {
    std::filesystem::path game_dir;
    int frames = -1;          // render N frames then exit (-1 = until quit)
    std::string screenshot;   // save first frame as BMP and exit
};

// Returns process exit code.
int run_viewer(const ViewerOptions& opts);

}  // namespace olduvai::presentation
