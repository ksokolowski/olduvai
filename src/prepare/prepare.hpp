// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Stage-1 prepare orchestration + cache status reporting.
//
// The prepare stage's job is to make a fresh first run fast and offline:
// it keys a per-fileset bucket on the game-file checksums + pipeline version
// and writes a manifest recording exactly which files (and digests) the
// bucket was built from.  `--verify-cache` reports present/valid/stale by
// comparing the live game-file key against the bucket on disk.
//
// STAGE-1 DECODED-ASSET CACHE STATUS — DEFERRED (scaffolded).
// A persisted decode cache (LZSS/MAT/PC1 → native pixels) is only safe if a
// cache-load is byte-identical to a fresh decode.  That round-trip is not
// proven in this pass, and the runtime decode is cheap, so we do NOT persist
// decoded assets yet — serving a subtly-different asset is far worse than a
// re-decode.  The bucket + manifest + key scheme are in place so the decode
// layer can drop in later behind the same key (see prepare.cpp TODO).
//
// Stage 2 (HD bake) persistence IS live — see enhance/hd_asset_cache.

#pragma once

#include <filesystem>
#include <string>

#include "prepare/game_files.hpp"

namespace olduvai::prepare {

enum class CacheState {
    kMissing,   // no bucket for the live game-file key
    kValid,     // bucket present and key-matches the live files
    kStale,     // a bucket exists but for different files / pipeline version
    kNoFiles,   // game files incomplete — can't form a key
};

struct CacheStatus {
    CacheState state = CacheState::kMissing;
    std::string key;                     // live game-file key ("" if no files)
    std::filesystem::path bucket;        // stage-1 bucket path for the key
    std::string message;                 // human-readable one-liner
};

// Inspect the cache for the detected game files (no side effects).
CacheStatus inspect_cache(const GameFiles& gf);

// Build (or rebuild) the stage-1 bucket for `gf`, printing progress to
// stdout.  Returns false on failure (e.g. incomplete files, unwritable
// cache dir).  Safe to call when a valid bucket already exists (it is
// rewritten — cheap and idempotent).
bool run_prepare(const GameFiles& gf, bool verbose = true);

// Ensure a valid stage-1 bucket exists for `gf`, building it if missing or
// stale.  Prints "Preparing game data…" on a (re)build, nothing on a hit.
// Returns false only on a hard failure; an incomplete fileset is the
// caller's problem to report (the engine still refuses to launch).
bool ensure_prepared(const GameFiles& gf);

}  // namespace olduvai::prepare
