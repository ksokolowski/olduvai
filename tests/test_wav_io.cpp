// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Minimal WAV I/O used by the HD SFX bake pipeline (decode-sfx export,
// bake output, enhanced-mode load).
#include "doctest/doctest.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "presentation/wav_io.hpp"

using olduvai::presentation::read_wav16;
using olduvai::presentation::write_wav16;

TEST_CASE("wav16 write/read roundtrip") {
    // temp_directory_path resolves TMPDIR/TEMP/TMP per platform — a raw
    // "/tmp" fallback does not exist on the Windows CI runners.
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "olduvai_test_roundtrip.wav";
    const std::vector<std::int16_t> pcm = {0, 100, -100, 32767, -32768, 42};
    REQUIRE(write_wav16(p, pcm, 22050, 1));
    int rate = 0, channels = 0;
    const auto back = read_wav16(p, rate, channels);
    CHECK(back == pcm);
    CHECK(rate == 22050);
    CHECK(channels == 1);
    std::filesystem::remove(p);
}

TEST_CASE("read_wav16 rejects junk") {
    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "olduvai_test_junk.wav";
    std::FILE* f = std::fopen(p.string().c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fputs("not a wav at all", f);
    std::fclose(f);
    int rate = 0, channels = 0;
    CHECK(read_wav16(p, rate, channels).empty());
    std::filesystem::remove(p);
}
