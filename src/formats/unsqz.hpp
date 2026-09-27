// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// SQZ decompressor: the LZW container some CD-era releases (GOG) ship the game
// executable in.  This variant:
//   - 4-byte header: byte 1 high nibble = 0x1 (format tag); uncompressed size
//     20 bits = (header[0] & 0x0F) << 16 | LE16(header+2)
//   - codes MSB-first, width 9 growing to 12 ("early change": when the next
//     free index reaches the current ceiling)
//   - 0x100 = dictionary clear, 0x101 = reset, not end of stream (executables
//     are one stream with periodic resets); the declared size terminates.
// Validated against the reference: a well-formed MZ image of exactly the
// declared size, and every table prepare reads matches the plain executable.

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
