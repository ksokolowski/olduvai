// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Core primitives: the 16-bit LCG random source and the 320x200 collision
// bitmap.  Expected RNG values are pinned from the validated reference
// implementation (same recurrence, same seed).

#include "doctest/doctest.h"
#include "core/collision_bitmap.hpp"
#include "core/rng.hpp"

using olduvai::core::CollisionBitmap;
using olduvai::core::RandLcg16;

TEST_CASE("LCG: documented first value for seed 1 is 0x015A (= 346)") {
    RandLcg16 rng;  // default seed 1
    CHECK(rng.next() == 0x015A);
}

TEST_CASE("LCG: reference sequence for seed 1") {
    RandLcg16 rng;
    const std::uint16_t expect[8] = {0x015A, 0x0082, 0x2AE6, 0x0442,
                                     0x2D88, 0x1BCD, 0x44BB, 0x190F};
    for (auto e : expect) CHECK(rng.next() == e);
    CHECK(rng.state() == 0x190F3A19u);
}

TEST_CASE("LCG: arbitrary seed parity") {
    RandLcg16 rng(0xDEADBEEFu);
    CHECK(rng.next() == 0x192B);
    CHECK(rng.next() == 0x5CD5);
    CHECK(rng.next() == 0x0BF3);
}

TEST_CASE("LCG: result is always 15-bit") {
    RandLcg16 rng(0xFFFFFFFFu);
    for (int i = 0; i < 1000; ++i) CHECK((rng.next() & ~0x7FFF) == 0);
}

// ── collision bitmap ──────────────────────────────────────────────────────

TEST_CASE("bitmap starts empty; set_bit makes a pixel solid") {
    CollisionBitmap bm;
    CHECK(bm.test(10, 10));          // empty = walkable = true
    bm.set_bit(10, 10);
    CHECK_FALSE(bm.test(10, 10));    // solid now
    CHECK(bm.test(11, 10));          // neighbour untouched
    CHECK(bm.test(10, 11));
    bm.clear();
    CHECK(bm.test(10, 10));
}

TEST_CASE("bitmap MSB-first packing within a row byte") {
    CollisionBitmap bm;
    bm.set_bit(0, 0);    // bit 7 of byte 0
    bm.set_bit(7, 0);    // bit 0 of byte 0
    bm.set_bit(8, 0);    // bit 7 of byte 1
    CHECK_FALSE(bm.test(0, 0));
    CHECK_FALSE(bm.test(7, 0));
    CHECK_FALSE(bm.test(8, 0));
    CHECK(bm.test(1, 0));
}

TEST_CASE("set_bit DISCARDS out-of-range coordinates") {
    CollisionBitmap bm;
    bm.set_bit(-1, 0);
    bm.set_bit(320, 0);
    bm.set_bit(0, -1);
    bm.set_bit(0, 200);
    // Nothing must have been written anywhere.
    for (int x : {0, 319}) {
        for (int y : {0, 199}) CHECK(bm.test(x, y));
    }
}

TEST_CASE("test CLAMPS x to 0..319 and y>199 to 199") {
    CollisionBitmap bm;
    bm.set_bit(0, 5);
    CHECK_FALSE(bm.test(-10, 5));    // x clamps to 0
    bm.set_bit(319, 199);
    CHECK_FALSE(bm.test(400, 250));  // both clamp to (319,199)
}

TEST_CASE("test treats negative y as 199 (16-bit unsigned-compare quirk)") {
    // The original reader compares the row register unsigned against 199,
    // so a negative y is a huge unsigned value and clamps to 199.
    CollisionBitmap bm;
    bm.set_bit(50, 199);
    CHECK_FALSE(bm.test(50, -3));
}

TEST_CASE("stamp_tile stamps DUR segments with horizontal bound check") {
    CollisionBitmap bm;
    using olduvai::formats::CollisionSegment;
    const std::vector<CollisionSegment> segs = {
        {0, 8, 4},     // 4 px at y+8 from x+0
        {-2, 15, 3},   // starts left of the tile; first px may clip at x<0
    };
    bm.stamp_tile(segs, 1, 100);
    CHECK_FALSE(bm.test(1, 108));
    CHECK_FALSE(bm.test(4, 108));
    CHECK(bm.test(5, 108));
    // segment 2: sx = -1, 0, 1 → -1 discarded, 0 and 1 set at y=115
    CHECK_FALSE(bm.test(0, 115));
    CHECK_FALSE(bm.test(1, 115));
    CHECK(bm.test(2, 115));
}
