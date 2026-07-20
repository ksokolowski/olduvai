// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// LZSS decompressor for CUR/VGA archive entries.
//
// A compact LZSS variant — LZSS is a generic dictionary coder that was
// ubiquitous in late-80s/early-90s DOS and Amiga games.  Parameters of
// this variant:
//   - bits packed MSB-first
//   - control bit 0: literal byte follows (8 bits)
//   - control bit 1: back-reference follows:
//       2 bits length code   -> length   = 2 + code  (2..5)
//       8 bits distance code -> distance = 1 + code  (1..256)
//   - sliding window: 256 bytes, pre-filled with 0x00
//   - payload is preceded by a UINT32BE uncompressed-size header
//
// Reference: https://moddingwiki.shikadi.net/wiki/LZSS_Compression
// Behaviour is kept bit-for-bit equivalent to the validated reference
// implementation (same window/state semantics, same early-stop on
// reaching the declared size mid-backreference).

#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace olduvai::formats {

class LzssError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Decompress an archive entry (including its 4-byte UINT32BE size header).
// Throws LzssError on truncated input or output-size mismatch.
std::vector<std::uint8_t> lzss_decompress(const std::uint8_t* data,
                                          std::size_t size);

inline std::vector<std::uint8_t>
lzss_decompress(const std::vector<std::uint8_t>& data) {
    return lzss_decompress(data.data(), data.size());
}

}  // namespace olduvai::formats
