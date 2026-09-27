// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The four archives (FILESA/B.CUR, FILESA/B.VGA), read once and indexed by
// entry name (60 entries, 0.94 MB decompressed, 9.3 ms).  A fixed search order
// is safe: the only names in two archives (BONUS.MDI, BONUSBUZ.MDI) are
// byte-identical.  Re-check if the archive set changes.  In prepare/, the
// lowest layer every caller may include.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace olduvai::prepare {

class GameArchives {
public:
    // Read and index all four.  A missing or malformed archive sets ok() false
    // (no throw); entries from the readable ones are kept.
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
