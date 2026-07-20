// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "formats/packbits.hpp"

namespace olduvai::formats {

std::vector<std::uint8_t> packbits_decompress(
    const std::vector<std::uint8_t>& data, std::size_t expected_size) {
    std::vector<std::uint8_t> out;
    out.reserve(expected_size);
    std::size_t i = 0;
    while (i < data.size() && out.size() < expected_size) {
        const std::uint8_t c = data[i++];
        if (c == 0x80) {            // no-op
            continue;
        }
        if (c <= 0x7F) {            // literal run: next (c + 1) bytes
            const std::size_t count = static_cast<std::size_t>(c) + 1;
            const std::size_t avail = data.size() - i;
            const std::size_t take = count < avail ? count : avail;
            out.insert(out.end(), data.begin() + static_cast<long>(i),
                       data.begin() + static_cast<long>(i + take));
            i += count;
        } else {                    // repeat run: next byte (257 - c) times
            const std::size_t count = 257 - static_cast<std::size_t>(c);
            if (i < data.size()) {
                out.insert(out.end(), count, data[i]);
                ++i;
            }
        }
    }
    if (out.size() > expected_size) out.resize(expected_size);
    return out;
}

}  // namespace olduvai::formats
