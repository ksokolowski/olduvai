// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// PC1 full-screen image — a standard IFF/ILBM container (the interchange
// format family that originated on the Amiga and was common across
// 16-bit-era tooling):
//
//   "FORM" u32be size "ILBM"
//     "BMHD" — width/height (u16be), nPlanes, compression
//     "CMAP" — 16 RGB triplets (values on a 0x10 grid; the loader uses
//               6-bit DAC precision: v >> 2, scaled to 8-bit for display)
//     "BODY" — pixel data, PackBits-compressed when compression == 1
//   (other chunks — DPPV, CRNG… — are skipped; chunks pad to even size)
//
// BODY planes are INTERLEAVED per scan line: each row stores nPlanes
// groups of ceil(width/8) bytes (plane 0 = bit 0 of the colour index).
// Bit 7 (MSB) = leftmost pixel.  Typical content: 320×200, 4 planes.

#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace olduvai::formats {

class Pc1Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const {
        return r == o.r && g == o.g && b == o.b;
    }
};

struct Pc1Image {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<Rgb> palette;            // 16 entries, 8-bit components
    std::vector<std::uint8_t> pixels;    // width × height palette indices
};

// `data` must already be LZSS-decompressed (CurArchive handles that).
Pc1Image parse_pc1(const std::vector<std::uint8_t>& data);

}  // namespace olduvai::formats
