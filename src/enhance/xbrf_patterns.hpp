// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// xBRF's pattern tables: which cells of a block each corner shape blends, and by how much.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace olduvai::enhance::xbrf {

// What the bottom-right corner becomes: a line that rises gently, rises tall,
// both at once, or runs on the diagonal; or a corner rounded on its own.
enum Shape : std::size_t { kGentle, kTall, kBoth, kDiagonal, kRound, kShapeCount };

// A pattern cell for the bottom-right corner: `part` blends the colour in by
// m/n, `whole` sets it (n is 0).
struct Cell {
    std::uint8_t row;
    std::uint8_t col;
    std::uint8_t m;
    std::uint8_t n;
};
using Pattern = std::vector<Cell>;
using PatternSet = std::array<Pattern, kShapeCount>;

// The patterns for factor s, 2 to 6.
const PatternSet& patterns_for(int s);

}  // namespace olduvai::enhance::xbrf
