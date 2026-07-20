// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Music-container conversion: implicit Note Off resolution, auto-release
// on re-strike, vel-0 release, preset injection, FF 7F strip, EOT append.
// (The full-coverage check runs out-of-repo: all 20 music files convert
// byte-identical to the reference engine.)

#include "doctest/doctest.h"
#include "formats/mdi.hpp"

using namespace olduvai::formats;

namespace {
std::vector<std::uint8_t> tiny_mdi() {
    // MThd (format 0, 1 track, division 96) + MTrk:
    //   d0 NoteOn ch0 n60 v100 · d10 NoteOn ch0 n62 v100 (auto-release 60)
    //   d10 NoteOff(0x80) ch0 n0 v0 (implicit -> 62) · d0 FF 7F len2 (strip)
    std::vector<std::uint8_t> t = {
        0x00, 0x90, 60, 100,
        0x0A, 0x90, 62, 100,
        0x0A, 0x80, 0, 0,
        0x00, 0xFF, 0x7F, 0x02, 0xAA, 0xBB,
    };
    std::vector<std::uint8_t> out = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
        'M','T','r','k',
        0, 0, 0, static_cast<std::uint8_t>(t.size()),
    };
    out.insert(out.end(), t.begin(), t.end());
    return out;
}
}  // namespace

TEST_CASE("note-off resolution + strip + EOT") {
    const auto out = build_gm_midi(tiny_mdi(), -1);
    // Header preserved.
    CHECK(out[0] == 'M');
    CHECK(out[13] == 96);
    // Track events: NoteOn 60 -> (auto-release 60, NoteOn 62) -> NoteOff 62.
    const std::vector<std::uint8_t> want = {
        0x00, 0x90, 60, 100,          // first strike
        0x0A, 0x80, 60, 0x00,         // auto-release on re-strike
        0x00, 0x90, 62, 100,
        0x0A, 0x80, 62, 0x00,         // implicit note-off resolved
        0x00, 0xFF, 0x2F, 0x00,       // EOT appended; FF 7F stripped
    };
    const std::vector<std::uint8_t> got(out.begin() + 22, out.end());
    CHECK(got == want);
}

TEST_CASE("preset injection emits program + volume per channel") {
    const auto out = build_gm_midi(tiny_mdi(), 2);   // jungle presets
    // First injected pair: ch1 -> program 28 then CC7=127 (0-based ch 0).
    CHECK(out[22] == 0x00);
    CHECK(out[23] == 0xC0);
    CHECK(out[24] == 28);
    CHECK(out[26] == 0xB0);
    CHECK(out[27] == 7);
    CHECK(out[28] == 127);
}
