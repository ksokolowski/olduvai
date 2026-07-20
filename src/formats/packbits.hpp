// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// PackBits decompression (IFF/ILBM variant) — the classic Apple/Amiga
// run-length scheme used throughout late-80s graphics formats.
//
//   control 0x80:        no-op (skip)
//   control 0x00..0x7F:  copy the next (n + 1) bytes literally
//   control 0x81..0xFF:  repeat the next byte (257 - n) times
//
// Applied as a single linear stream; output stops at `expected_size`.

#pragma once

#include <cstdint>
#include <vector>

namespace olduvai::formats {

std::vector<std::uint8_t> packbits_decompress(
    const std::vector<std::uint8_t>& data, std::size_t expected_size);

}  // namespace olduvai::formats
