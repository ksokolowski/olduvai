// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The game's four archives, opened once and indexed by entry name.
//
// FILESA.CUR, FILESB.CUR, FILESA.VGA and FILESB.VGA were opened and scanned
// in-place at a dozen sites, each repeating the same four constructions and
// the same linear "which archive has this name?" loop.
//
// MEASURED on a full game copy: 60 entries across the four archives, 58 unique
// names, 0.94 MB decompressed, 9.3 ms to read, decompress and index all of it.
// Holding that is free, and it replaces a per-lookup scan over four archives
// with one map lookup.
//
// A FIXED search order is safe here, and that is a measured claim rather than
// an assumption: exactly two names appear in more than one archive
// (BONUS.MDI, BONUSBUZ.MDI) and both are byte-identical, so first-wins cannot
// resolve differently from any other order.  Call sites that deliberately
// searched .CUR-before-.VGA for one asset and .VGA-before-.CUR for another are
// therefore unaffected.  Re-check with that scan if the archive set ever
// changes.
//
// Lives in prepare/ because it needs formats::CurArchive and slurp_file, and
// prepare is the lowest layer every caller (presentation, app, tools) may
// legally include.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace olduvai::prepare {

class GameArchives {
public:
    // Reads and indexes all four archives.  A missing or malformed archive
    // leaves ok() false rather than throwing across the module boundary;
    // partial results are kept, since a caller wanting one entry from a
    // readable archive should not be denied it by an unrelated bad one.
    explicit GameArchives(const std::filesystem::path& game_dir);

    // The entry's decompressed bytes, or nullptr when no archive has it.
    // The pointer is owned here and stays valid for this object's lifetime.
    const std::vector<std::uint8_t>* entry(const std::string& name) const;

    bool contains(const std::string& name) const { return entry(name) != nullptr; }

    // False when any archive failed to open or parse.  `why()` says which.
    bool ok() const { return why_.empty(); }
    const std::string& why() const { return why_; }

    std::size_t size() const { return by_name_.size(); }

private:
    std::map<std::string, std::vector<std::uint8_t>> by_name_;
    std::string why_;
};

}  // namespace olduvai::prepare
