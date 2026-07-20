// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Content-addressed paths for the HD SFX bake pipeline.
//   --decode-sfx           → <cache>/hd_sfx_src/<digest>.wav   (engine writes)
//   scripts/bake_hd_sfx.py → <cache>/hd_sfx/<digest>.wav       (tool writes)
//   load_sfx               → reads hd_sfx/<digest>.wav in enhanced mode
// The digest is FNV-1a/64 over the DECODED sample bytes — same digest family
// as the prepare cache keys.  Same sample ⇒ same name on both sides.
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "prepare/cache_paths.hpp"

namespace olduvai::presentation {

inline std::string sfx_digest_hex(const std::vector<std::uint8_t>& d) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const std::uint8_t b : d) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
}

inline std::filesystem::path hd_sfx_dir() {
    return prepare::cache_root() / "hd_sfx";
}

inline std::filesystem::path hd_sfx_src_dir() {
    return prepare::cache_root() / "hd_sfx_src";
}

}  // namespace olduvai::presentation
