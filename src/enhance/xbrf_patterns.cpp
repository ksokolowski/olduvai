// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// xBRF's pattern tables, for the bottom-right corner; xbrf.cpp turns them to the other three.
#include "enhance/xbrf_patterns.hpp"

namespace olduvai::enhance::xbrf {

namespace {

constexpr Cell part(int r, int c, int m, int n) {
    return {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(m), static_cast<std::uint8_t>(n)};
}
constexpr Cell whole(int r, int c) { return part(r, c, 0, 0); }

// One table per factor, a pattern per shape in Shape's order, its cells by row
// and then column of the block.
const PatternSet& patterns_two() {
    static const PatternSet kTwo = {{
        // gentle
        {part(1, 0, 1, 4), part(1, 1, 3, 4)},
        // tall
        {part(0, 1, 1, 4),
         part(1, 1, 3, 4)},
        // both
        {part(0, 1, 1, 4),
         part(1, 0, 1, 4), part(1, 1, 5, 6)},
        // diagonal
        {part(1, 1, 1, 2)},
        // round
        {part(1, 1, 21, 100)},
    }};
    return kTwo;
}

const PatternSet& patterns_three() {
    static const PatternSet kThree = {{
        // gentle
        {part(1, 2, 1, 4),
         part(2, 0, 1, 4), part(2, 1, 3, 4), whole(2, 2)},
        // tall
        {part(0, 2, 1, 4),
         part(1, 2, 3, 4),
         part(2, 1, 1, 4), whole(2, 2)},
        // both
        {part(0, 2, 1, 4),
         part(1, 2, 3, 4),
         part(2, 0, 1, 4), part(2, 1, 3, 4), whole(2, 2)},
        // diagonal
        {part(1, 2, 1, 8),
         part(2, 1, 1, 8), part(2, 2, 7, 8)},
        // round
        {part(2, 2, 45, 100)},
    }};
    return kThree;
}

const PatternSet& patterns_four() {
    static const PatternSet kFour = {{
        // gentle
        {part(2, 2, 1, 4), part(2, 3, 3, 4),
         part(3, 0, 1, 4), part(3, 1, 3, 4), whole(3, 2), whole(3, 3)},
        // tall
        {part(0, 3, 1, 4),
         part(1, 3, 3, 4),
         part(2, 2, 1, 4), whole(2, 3),
         part(3, 2, 3, 4), whole(3, 3)},
        // both
        {part(0, 3, 1, 4),
         part(1, 3, 3, 4),
         part(2, 2, 1, 3), whole(2, 3),
         part(3, 0, 1, 4), part(3, 1, 3, 4), whole(3, 2), whole(3, 3)},
        // diagonal
        {part(2, 3, 1, 2),
         part(3, 2, 1, 2), whole(3, 3)},
        // round
        {part(2, 3, 9, 100),
         part(3, 2, 9, 100), part(3, 3, 68, 100)},
    }};
    return kFour;
}

const PatternSet& patterns_five() {
    static const PatternSet kFive = {{
        // gentle
        {part(2, 4, 1, 4),
         part(3, 2, 1, 4), part(3, 3, 3, 4), whole(3, 4),
         part(4, 0, 1, 4), part(4, 1, 3, 4), whole(4, 2), whole(4, 3), whole(4, 4)},
        // tall
        {part(0, 4, 1, 4),
         part(1, 4, 3, 4),
         part(2, 3, 1, 4), whole(2, 4),
         part(3, 3, 3, 4), whole(3, 4),
         part(4, 2, 1, 4), whole(4, 3), whole(4, 4)},
        // both
        {part(0, 4, 1, 4),
         part(1, 4, 3, 4),
         part(2, 3, 1, 4), whole(2, 4),
         part(3, 2, 1, 4), part(3, 3, 2, 3), whole(3, 4),
         part(4, 0, 1, 4), part(4, 1, 3, 4), whole(4, 2), whole(4, 3), whole(4, 4)},
        // diagonal
        {part(2, 4, 1, 8),
         part(3, 3, 1, 8), part(3, 4, 7, 8),
         part(4, 2, 1, 8), part(4, 3, 7, 8), whole(4, 4)},
        // round
        {part(3, 4, 23, 100),
         part(4, 3, 23, 100), part(4, 4, 86, 100)},
    }};
    return kFive;
}

const PatternSet& patterns_six() {
    static const PatternSet kSix = {{
        // gentle
        {part(3, 4, 1, 4), part(3, 5, 3, 4),
         part(4, 2, 1, 4), part(4, 3, 3, 4), whole(4, 4), whole(4, 5),
         part(5, 0, 1, 4), part(5, 1, 3, 4), whole(5, 2), whole(5, 3), whole(5, 4), whole(5, 5)},
        // tall
        {part(0, 5, 1, 4),
         part(1, 5, 3, 4),
         part(2, 4, 1, 4), whole(2, 5),
         part(3, 4, 3, 4), whole(3, 5),
         part(4, 3, 1, 4), whole(4, 4), whole(4, 5),
         part(5, 3, 3, 4), whole(5, 4), whole(5, 5)},
        // both
        {part(0, 5, 1, 4),
         part(1, 5, 3, 4),
         part(2, 4, 1, 4), whole(2, 5),
         part(3, 4, 3, 4), whole(3, 5),
         part(4, 2, 1, 4), part(4, 3, 3, 4), whole(4, 4), whole(4, 5),
         part(5, 0, 1, 4), part(5, 1, 3, 4), whole(5, 2), whole(5, 3), whole(5, 4), whole(5, 5)},
        // diagonal
        {part(3, 5, 1, 2),
         part(4, 4, 1, 2), whole(4, 5),
         part(5, 3, 1, 2), whole(5, 4), whole(5, 5)},
        // round
        {part(3, 5, 6, 100),
         part(4, 5, 42, 100),
         part(5, 3, 6, 100), part(5, 4, 42, 100), part(5, 5, 97, 100)},
    }};
    return kSix;
}

}  // namespace

const PatternSet& patterns_for(int s) {
    constexpr int kFirst = 2;
    using Table = const PatternSet& (*)();
    static constexpr std::array<Table, 5> kAll = {patterns_two, patterns_three, patterns_four, patterns_five, patterns_six};
    return kAll[static_cast<std::size_t>(s - kFirst)]();
}

}  // namespace olduvai::enhance::xbrf
