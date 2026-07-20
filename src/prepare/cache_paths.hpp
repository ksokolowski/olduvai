// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Platform cache-directory resolution + the cache key/path scheme for the
// first-run prepare-and-cache pipeline.
//
// Olduvai ships NO game-derived data.  At first run it reads the user's own
// game files and prepares a local cache under the platform cache dir:
//   Linux:   $XDG_CACHE_HOME/olduvai  (default ~/.cache/olduvai)
//   macOS:   ~/Library/Caches/olduvai
//   Windows: %LOCALAPPDATA%\olduvai\cache
// The $OLDUVAI_CACHE_DIR environment variable overrides all of the above
// (used by tests and by power users).  Nothing game-derived is ever written
// into the repository or the install directory.
//
// Layout under the cache root:
//   <root>/<key>/                 per-game-fileset prepare bucket (stage 1)
//   <root>/<key>/manifest.txt     prepare manifest (key + version + files)
//   <root>/hd/<hdkey>.bin         HD-baked upscale blocks (stage 2)
//
// The prepare key combines the game-file checksums with a pipeline version
// constant; bumping the version invalidates every stage-1 bucket without
// touching user files.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace olduvai::prepare {

// Bump when the prepare pipeline's *output* changes in a way that makes an
// existing stage-1 cache wrong (decoder fix, table-layout change, …).  An
// old bucket then key-mismatches and is rebuilt.
inline constexpr int kPipelineVersion = 1;

// Absolute path to the cache root, honouring $OLDUVAI_CACHE_DIR, then
// $XDG_CACHE_HOME (Linux), then the per-platform default.  Does NOT create
// the directory — call ensure_cache_dir() for that.
std::filesystem::path cache_root();

// The stage-1 (prepare) bucket for a given combined game-file key.
// <root>/<key>
std::filesystem::path prepare_bucket(const std::string& key);

// The stage-2 (HD bake) directory.  <root>/hd
std::filesystem::path hd_dir();

// Create `dir` (and parents) if absent.  Returns false on failure.
bool ensure_cache_dir(const std::filesystem::path& dir);

// Recursively delete the whole cache root.  Returns false on failure.
// A non-existent root counts as success (nothing to purge).
bool purge_cache();

}  // namespace olduvai::prepare
