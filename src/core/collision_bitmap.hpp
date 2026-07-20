// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// 320x200 binary collision bitmap (40 bytes/row, MSB-first), the ground/
// wall probe surface for player and monster logic.
//
// Semantics mirror the original routines exactly, including the asymmetry:
//   set_bit DISCARDS out-of-range coordinates,          // FUN_27f7_003b
//   test CLAMPS x to 0..319 and y to 199 — a negative y
//   behaves as 199 via the unsigned 16-bit row compare. // FUN_27f7_135e
// stamp_tile writes DUR segments with a horizontal
// 0 <= x < 320 check per pixel.                          // FUN_27f7_006c

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "formats/dur.hpp"

namespace olduvai::core {

class CollisionBitmap {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 200;

    void clear() { data_.fill(0); }

    void set_bit(int x, int y) {
        if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
        data_[static_cast<std::size_t>(y) * 40 + (x >> 3)] |=
            static_cast<std::uint8_t>(0x80u >> (x & 7));
    }

    // True = empty (walkable), false = solid.
    bool test(int x, int y) const {
        if (x < 0) x = 0;
        else if (x > 319) x = 319;
        if (y < 0) y = 199;   // unsigned 16-bit compare quirk: negative → 199
        else if (y > 199) y = 199;
        const std::uint8_t b =
            data_[static_cast<std::size_t>(y) * 40 + (x >> 3)];
        return (b & (0x80u >> (x & 7))) == 0;
    }

    void stamp_tile(const std::vector<formats::CollisionSegment>& segments,
                    int x, int y) {
        for (const auto& seg : segments) {
            int sx = seg.dx + x;
            const int sy = seg.dy + y;
            for (int i = 0; i < seg.width; ++i, ++sx) {
                if (sx >= 0 && sx < kWidth) set_bit(sx, sy);
            }
        }
    }

private:
    std::array<std::uint8_t, 8000> data_{};
};

}  // namespace olduvai::core
