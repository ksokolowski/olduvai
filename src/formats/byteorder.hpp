// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Little/big-endian fixed-width integer readers over a byte buffer.  The only
// genuinely shared abstraction in formats/: read_u16be was bit-identical in
// pc1.cpp and mat.cpp, read_u16le in cur.cpp and dur.cpp (audit C3).  Bounds
// are the caller's responsibility (the decoders validate lengths up front);
// these are pure index math, pinned by the byte-compare decoder doctests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace olduvai::formats {

inline std::uint16_t read_u16be(const std::vector<std::uint8_t>& d,
                                std::size_t p) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(d[p]) << 8) | d[p + 1]);
}

inline std::uint32_t read_u32be(const std::vector<std::uint8_t>& d,
                                std::size_t p) {
    return (static_cast<std::uint32_t>(d[p]) << 24) |
           (static_cast<std::uint32_t>(d[p + 1]) << 16) |
           (static_cast<std::uint32_t>(d[p + 2]) << 8) |
           static_cast<std::uint32_t>(d[p + 3]);
}

inline std::uint16_t read_u16le(const std::vector<std::uint8_t>& d,
                                std::size_t p) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(d[p]) |
        (static_cast<std::uint16_t>(d[p + 1]) << 8));
}

inline std::uint32_t read_u32le(const std::vector<std::uint8_t>& d,
                                std::size_t p) {
    return static_cast<std::uint32_t>(d[p]) |
           (static_cast<std::uint32_t>(d[p + 1]) << 8) |
           (static_cast<std::uint32_t>(d[p + 2]) << 16) |
           (static_cast<std::uint32_t>(d[p + 3]) << 24);
}

}  // namespace olduvai::formats
