// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Creative Voice File (VOC) reader — the standard v1.10 layout: 26-byte
// header ("Creative Voice File\x1A", header size, version, the
// ~version+0x1234 checksum), then typed blocks.  The game's effects use
// type-1 sound blocks: u8 time constant (rate = 1000000/(256-tc)),
// u8 codec (0 = 8-bit unsigned PCM), then the sample data.

#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::formats {

struct VocAudio {
    int sample_rate = 0;
    int codec = 0;
    std::vector<std::uint8_t> data;   // 8-bit unsigned PCM
};

struct VocFile {
    std::vector<VocAudio> blocks;

    // First audio block (the game's effects carry exactly one).
    const VocAudio* audio() const {
        return blocks.empty() ? nullptr : &blocks.front();
    }
};

VocFile parse_voc(const std::vector<std::uint8_t>& raw);

}  // namespace olduvai::formats
