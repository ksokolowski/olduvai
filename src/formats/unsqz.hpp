// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SQZ decompressor — the LZW-compressed container some CD-era
// distributions (notably the GOG release) ship the game executable in,
// loaded there by a small self-extracting runner instead of a plain file.
//
// LZW is the classic generic dictionary coder (GIF, compress, and many
// DOS-era packers).  Parameters of this variant:
//   - 4-byte header: byte 1 high nibble = 0x1 (format tag);
//     uncompressed size = 20 bits, (header[0] & 0x0F) << 16 | LE16(header+2)
//   - codes packed MSB-first, width 9 growing to 12 as the dictionary
//     fills (width grows when the next-free index reaches the current
//     code ceiling — "early change")
//   - code 0x100 = dictionary clear, 0x101 = dictionary reset
//   - 0x101 does NOT terminate the stream: executables are packed as one
//     continuous stream with periodic full resets (unlike the data-file
//     flavour of the same coder, where 0x101 is the end marker).  The
//     declared output size is the sole terminator.
//
// Behaviour validated against the reference implementation: the decoded
// image byte-compares as a well-formed MZ executable of exactly the
// declared size, and every table the prepare pipeline reads from it
// matches the plain-file executable (see prepare/exe_tables.cpp).

#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace olduvai::formats {

class SqzError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Cheap signature probe (header nibble + declared-size sanity); safe on
// arbitrary bytes.  A true result does not guarantee a valid stream.
bool looks_like_sqz(const std::uint8_t* data, std::size_t size);

inline bool looks_like_sqz(const std::vector<std::uint8_t>& data) {
    return looks_like_sqz(data.data(), data.size());
}

// Decompress a whole SQZ container (including its 4-byte header).
// Throws SqzError on malformed / truncated input.
std::vector<std::uint8_t> unsqz(const std::uint8_t* data, std::size_t size);

inline std::vector<std::uint8_t> unsqz(const std::vector<std::uint8_t>& data) {
    return unsqz(data.data(), data.size());
}

}  // namespace olduvai::formats
