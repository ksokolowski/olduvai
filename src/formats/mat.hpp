// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// MAT sprite/tileset container.
//
// All integers big-endian.  Layout:
//   UINT16BE count
//   per sprite: UINT16BE width, UINT16BE height, UINT16BE iSize
//     iSize == 0 → raw pixel data follows:
//         monochrome container: ceil(w/8) × h bytes (1bpp, single plane)
//         colour container:     ceil(w/8) × 5 × h bytes (5 bitplanes)
//     iSize > 0  → iSize bytes of PackBits-compressed data inflating to
//         ceil(w/8) × 5 × h bytes
//
// 5-plane pixel layout is NON-INTERLEAVED (contiguous plane blocks):
//   plane0..plane3 = bits 0..3 of the 4-bit colour index, then a 1-bit
//   opacity mask plane.  Within each byte, bit 7 (MSB) is the leftmost
//   pixel.  The font container (CHARSET1) is the lone 1bpp monochrome
//   case.  There is no hotspot field in the on-disk format.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace olduvai::formats {

enum class SpriteFormat { Ilbm5, Mono1bpp };

struct IndexedPixel {
    std::uint8_t color = 0;  // 4-bit colour index (0..15); mono: 15 when set
    bool opaque = false;
};

struct Sprite {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    SpriteFormat format = SpriteFormat::Ilbm5;
    std::vector<std::uint8_t> raw_pixels;  // uncompressed plane data

    // Decode to width × height indexed pixels (row-major).
    std::vector<IndexedPixel> decode_indexed() const;
};

class MatFile {
public:
    // `name` selects the monochrome exception (the CHARSET1 font container).
    MatFile(const std::vector<std::uint8_t>& data, const std::string& name);

    const std::vector<Sprite>& sprites() const { return sprites_; }

private:
    std::vector<Sprite> sprites_;
};

}  // namespace olduvai::formats
