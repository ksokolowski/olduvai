// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "formats/voc.hpp"

#include <cstring>
#include <stdexcept>

namespace olduvai::formats {

VocFile parse_voc(const std::vector<std::uint8_t>& raw) {
    if (raw.size() < 26) throw std::runtime_error("voc: file too small");
    static const char kSig[] = "Creative Voice File";
    if (std::memcmp(raw.data(), kSig, sizeof(kSig) - 1) != 0) {
        throw std::runtime_error("voc: bad signature");
    }
    const std::size_t header_size = raw[20] | (raw[21] << 8);
    const unsigned version = raw[22] | (raw[23] << 8);
    const unsigned checksum = raw[24] | (raw[25] << 8);
    if (((~version + 0x1234) & 0xFFFF) != checksum) {
        throw std::runtime_error("voc: checksum mismatch");
    }
    VocFile out;
    std::size_t pos = header_size;
    while (pos < raw.size()) {
        const std::uint8_t block_type = raw[pos];
        if (block_type == 0) break;   // terminator
        ++pos;
        if (pos + 3 > raw.size()) break;
        const std::size_t block_size =
            raw[pos] | (raw[pos + 1] << 8) | (raw[pos + 2] << 16);
        pos += 3;
        if (block_type == 1 && pos + 2 <= raw.size()) {
            const int tc = raw[pos];
            VocAudio a;
            a.sample_rate = 1000000 / (256 - tc);
            a.codec = raw[pos + 1];
            const std::size_t end = std::min(pos + block_size, raw.size());
            a.data.assign(raw.begin() + static_cast<std::ptrdiff_t>(pos + 2),
                          raw.begin() + static_cast<std::ptrdiff_t>(end));
            out.blocks.push_back(std::move(a));
        }
        pos += block_size;
    }
    return out;
}

}  // namespace olduvai::formats
