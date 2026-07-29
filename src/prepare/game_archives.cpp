// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "prepare/game_archives.hpp"

#include <exception>
#include <utility>

#include "formats/cur.hpp"
#include "prepare/game_files.hpp"

namespace olduvai::prepare {

GameArchives::GameArchives(const std::filesystem::path& game_dir) {
    // Order is the historical scan order (.CUR pair, then .VGA pair).  With no
    // differing duplicates it does not affect which bytes win — see the header
    // — but keeping it makes a diff against the old sites trivial to read.
    static const char* const kArchives[] = {"FILESA.CUR", "FILESB.CUR",
                                            "FILESA.VGA", "FILESB.VGA"};
    for (const char* name : kArchives) {
        try {
            const formats::CurArchive ar(slurp_file(game_dir / name));
            for (const auto& e : ar.entries()) {
                // First wins: the historical loops returned the first archive
                // that contained the name.
                by_name_.emplace(e.name, e.data);
            }
        } catch (const std::exception& e) {
            if (!why_.empty()) why_ += "; ";
            why_ += std::string(name) + ": " + e.what();
        }
    }
}

const std::vector<std::uint8_t>* GameArchives::entry(
    const std::string& name) const {
    const auto it = by_name_.find(name);
    return it == by_name_.end() ? nullptr : &it->second;
}

}  // namespace olduvai::prepare
