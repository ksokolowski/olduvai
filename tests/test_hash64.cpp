// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// formats::Hash64 — the one key mixer (src/formats/hash64.hpp).
//
// The values below are PINNED on purpose.  This mixer's output is baked into
// things that outlive a run: the HD asset cache's keys, the game-file
// identity checksums that decide whether a user's files are recognised, and
// the SFX digests the cross-platform audio gate compares.  A change here
// would be invisible in behaviour and catastrophic in effect, so the numbers
// are written down rather than derived from the code under test.  They were
// computed independently (a four-line Python reimplementation), not copied
// from a run of this class.
#include "doctest/doctest.h"
#include "formats/hash64.hpp"

#include <cstdint>
#include <string>
#include <vector>

using olduvai::formats::Hash64;

TEST_CASE("Hash64: the pinned values of the tree's mixer") {
    CHECK(Hash64{}.value() == 1469598103934665603ull);   // the offset basis

    Hash64 abc;
    abc.mix_str("abc");
    CHECK(abc.value() == 0xe16801510db89efdull);

    // Two scalars, the key-builder path (bg_compose, widescreen overlay).
    Hash64 two;
    two.mix(1);
    two.mix(2);
    CHECK(two.value() == 0x9a65ab00c545d26cull);
}

TEST_CASE("Hash64: bytes and scalars are the same operation") {
    const std::vector<std::uint8_t> bytes{'a', 'b', 'c'};
    Hash64 a;
    a.mix_bytes(bytes.data(), bytes.size());
    Hash64 b;
    for (const std::uint8_t x : bytes) b.mix(x);
    Hash64 c;
    c.mix_str(std::string(bytes.begin(), bytes.end()));
    CHECK(a.value() == b.value());
    CHECK(a.value() == c.value());
}

TEST_CASE("Hash64: order matters, and so does a separator") {
    Hash64 ab;
    ab.mix_str("ab");
    ab.mix_str("c");
    Hash64 a_bc;
    a_bc.mix_str("a");
    a_bc.mix_str("bc");
    // Without a separator these two ARE equal — the reason every key builder
    // in the tree mixes one between variable-length parts (overlay_key's
    // 0x1 marker).  Pinned so the header's advice stays true.
    CHECK(ab.value() == a_bc.value());

    Hash64 sep1;
    sep1.mix_str("ab");
    sep1.mix(0x1);
    sep1.mix_str("c");
    Hash64 sep2;
    sep2.mix_str("a");
    sep2.mix(0x1);
    sep2.mix_str("bc");
    CHECK(sep1.value() != sep2.value());
}
