// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// DUR tile-collision table.
//
// Exactly 960 bytes: 40 entries × 24 bytes (entries 0-19 = surface tile
// types, 20-39 = cave tile types).  Each entry holds up to 4 horizontal
// collision segments of {INT16LE dx, INT16LE dy, UINT16LE width};
// a dx of -1 terminates the list.  An all-zero (0,0,0) triple is a
// width-0 no-op in the original collision writer; the parser treats it as
// end-of-list (the resulting collision bitmaps are identical).

#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace olduvai::formats {

class DurError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CollisionSegment {
    std::int16_t dx = 0;
    std::int16_t dy = 0;
    std::uint16_t width = 0;
};

struct TileCollision {
    int index = 0;
    std::vector<CollisionSegment> segments;
};

struct DurLevel {
    std::vector<TileCollision> tiles;  // always 40
};

DurLevel parse_dur(const std::vector<std::uint8_t>& data);

}  // namespace olduvai::formats
