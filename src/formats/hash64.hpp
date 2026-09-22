// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The 64-bit key mixer this tree hashes with — one definition.
//
// IN formats/, THE LOWEST LAYER, because its users span the tree: prepare/
// (the EXE digest, the game-file identity) and presentation/ (the SFX
// digest, the static-background and overlay keys).  It started in core/,
// which is ABOVE prepare — `check_layers` said so on CI while the local gate,
// which ran no lints at all, stayed green (both are fixed in the commit that
// moved this file).  "Bytes in, a number out" is this layer's contract.
//
// Seven files carried the same two constants and the same two lines
// (`h ^= v; h *= prime`): the EXE table digest, the SFX digest, the game-file
// identity, the static-background key, the overlay key, the HD asset key and
// the overlay's row scan.  Found by `scripts/metrics/shape_clones.py`, which
// looks for a repeated SEQUENCE OF CALLS rather than repeated text.
//
// THE CONSTANT IS NOT THE STANDARD ONE, and is kept exactly as it is.
// FNV-1a's 64-bit offset basis is 14695981039346656037; this tree has always
// used 1469598103934665603 — the same digits one short.  It is a fine mixer
// either way, every site used the same value, and the value is baked into
// keys that persist (the HD asset cache) and into digests a test compares
// across runs.  "Correcting" it would change every key in the tree to fix
// nothing.  Recorded here so the next reader does not have to rediscover it,
// and does not quietly "fix" it either.
//
// WHAT DOES NOT USE THIS, and why:
//   * `text_overlay.cpp`'s row scan — the same mix, fused into a per-frame
//     pixel loop that also computes each row's used/unused flag.  Lifting it
//     out would split one pass into two for no gain.
//   * `hd_asset_cache.cpp`'s `key_of` — mixes its SCALARS with the
//     shift-and-add combine (`v + 0x9e3779b9... + (k<<6) + (k>>2)`), not with
//     this one.  Different function, deliberately left alone: its output is a
//     cache key with entries already written under it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace olduvai::formats {

inline constexpr std::uint64_t kHash64Offset = 1469598103934665603ull;
inline constexpr std::uint64_t kHash64Prime = 1099511628211ull;

class Hash64 {
public:
    // One value into the running hash.  Callers feeding a sequence of scalars
    // (a key builder) want a separator between variable-length parts —
    // "ab","c" and "a","bc" mix identically otherwise.
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
