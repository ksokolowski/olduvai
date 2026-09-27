// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Game-file detection and checksumming: find the five required files in a
// directory (case-insensitive) and FNV-1a/64 each.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace olduvai::prepare {

// The file's bytes, or empty if it cannot be opened (missing and empty look
// the same; test existence separately).  In prepare/, the lowest layer every
// caller may include.
std::vector<std::uint8_t> slurp_file(const std::filesystem::path& p);

// The required game files, in a fixed order.
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

};

// Detect and checksum the required files in `game_dir`; absent files get
// present=false (never throws).  HISTORIK.EXE or PREH.SQZ (the CD-era /
// GOG form) satisfies the executable: the entry keeps its canonical name with
// via_sqz set, checksummed over the bytes on disk.
GameFiles detect_game_files(const std::filesystem::path& game_dir);

// The directory actually holding the files: `dir` if it has FILESA.CUR, else
// the GOG layouts <dir>/data/PREH and <dir>/PREH (case-insensitive), else
// `dir`.
std::filesystem::path resolve_game_dir(const std::filesystem::path& dir);

// Directories to probe when none is configured: the GOG install (Windows: the
// installer registry entry, then the usual Galaxy / standalone paths; empty
// elsewhere).  Unverified; detect_game_files() each.
std::vector<std::filesystem::path> default_game_dir_candidates();

// The executable image for the table readers: HISTORIK.EXE as-is or PREH.SQZ
// decoded.  Empty if neither is present or the SQZ is malformed; never throws.
std::vector<std::uint8_t> load_game_executable(
    const std::filesystem::path& game_dir);

// FNV-1a/64 of a file's bytes.  Returns 0 and sets ok=false when the file
// can't be read (missing / unreadable); sets ok=true and size on success.
std::uint64_t fnv1a64_file(const std::filesystem::path& path, bool& ok,
                           std::uintmax_t& size);

}  // namespace olduvai::prepare
