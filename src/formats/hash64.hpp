// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The 64-bit key mixer used across the tree (EXE digest, game-file identity,
// SFX digest, static-background and overlay keys), in formats/, the lowest
// layer, because users span prepare/ and presentation/.
// The offset basis is 1469598103934665603, one digit short of FNV-1a's
// standard 14695981039346656037.  Keep it: persistent keys (the HD asset
// cache) and test digests are built on it.
// Not used by: text_overlay.cpp's row scan (the same mix fused into a per-pixel
// pass) and hd_asset_cache.cpp's key_of (a different scalar combine; existing
// cache keys depend on it).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace olduvai::formats {

inline constexpr std::uint64_t kHash64Offset = 1469598103934665603ull;
inline constexpr std::uint64_t kHash64Prime = 1099511628211ull;

class Hash64 {
public:
    // One value into the hash.  Separate variable-length parts ("ab","c" and
    // "a","bc" mix the same otherwise).
    void mix(std::uint64_t v) {
        h_ ^= v;
        h_ *= kHash64Prime;
    }

    void mix_bytes(const std::uint8_t* p, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) mix(p[i]);
    }

    void mix_str(const std::string& s) {
        for (const char c : s) mix(static_cast<unsigned char>(c));
    }

    std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = kHash64Offset;
};

}  // namespace olduvai::formats
