// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Game-file detection and checksumming for the prepare pipeline.
//
// Locates the five required Prehistorik files in a game directory
// (case-insensitively — DOS media is often upper-case, extracted copies
// often lower), checksums each with FNV-1a/64, and folds the per-file
// digests plus the pipeline version into one stable cache key.  The key is
// the directory name of the stage-1 prepare bucket, so any byte change in
// any game file (or a pipeline-version bump) yields a fresh bucket.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace olduvai::prepare {

// The required game files, in the order they fold into the combined key.
// (Order is fixed so the key is stable across runs/platforms.)
const std::vector<std::string>& required_game_files();

struct GameFileInfo {
    std::string name;                  // canonical upper-case name
    std::filesystem::path path;        // resolved on-disk path ("" if missing)
    bool present = false;              // file exists
    bool zero_byte = false;            // exists but empty (corrupt/placeholder)
    bool via_sqz = false;              // executable provided as PREH.SQZ
    std::uint64_t checksum = 0;        // FNV-1a/64 over the bytes (0 if absent)
    std::uintmax_t size = 0;
};

struct GameFiles {
    std::filesystem::path dir;
    std::vector<GameFileInfo> files;   // one per required_game_files()

    // True only when every required file is present and non-empty.
    bool complete() const;

    // Human-readable list of problems (missing / zero-byte), one per line.
    // Empty when complete() is true.
    std::string problems() const;

    // Combined cache key: hex of an FNV-1a/64 fold over the pipeline version
    // and each file's checksum.  Only meaningful when complete(); returns ""
    // otherwise (a partial fileset must never produce a usable key).
    std::string cache_key() const;
};

// Detect + checksum the required files in `game_dir`.  Files that are absent
// are recorded with present=false; this never throws on a missing file.
//
// The executable requirement is satisfied by either HISTORIK.EXE or
// PREH.SQZ (the compressed form CD-era distributions such as GOG ship it
// in); the entry keeps its canonical name with via_sqz set and the
// checksum taken over the actual on-disk bytes.
GameFiles detect_game_files(const std::filesystem::path& game_dir);

// Map a user-supplied directory to the one actually holding the game
// files.  Returns `dir` itself when it contains FILESA.CUR; otherwise
// probes the known GOG-install layout (<dir>/data/PREH, <dir>/PREH,
// case-insensitively) so `--game-dir <GOG install root>` just works.
// Falls back to `dir` unchanged when nothing matches.
std::filesystem::path resolve_game_dir(const std::filesystem::path& dir);

// Directories worth probing when the user configured no game directory at
// all: the machine's GOG install of the game.  On Windows this is the
// GOG-installer registry entry (works for custom install paths) followed
// by the conventional GOG Galaxy / standalone locations; empty elsewhere.
// Candidates are returned unverified — detect_game_files() each.
std::vector<std::filesystem::path> default_game_dir_candidates();

// Read the executable image the table readers consume: HISTORIK.EXE bytes
// as-is, or PREH.SQZ decoded, whichever detection would pick.  Returns an
// empty vector when neither is present or the SQZ container is malformed
// (never throws across the module boundary).
std::vector<std::uint8_t> load_game_executable(
    const std::filesystem::path& game_dir);

// FNV-1a/64 of a file's bytes.  Returns 0 and sets ok=false when the file
// can't be read (missing / unreadable); sets ok=true and size on success.
std::uint64_t fnv1a64_file(const std::filesystem::path& path, bool& ok,
                           std::uintmax_t& size);

}  // namespace olduvai::prepare
