// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "formats/dur.hpp"

namespace olduvai::formats {

namespace {

constexpr std::size_t kEntryCount = 40;
constexpr std::size_t kEntryStride = 24;
constexpr std::size_t kMaxSegments = 4;

std::int16_t read_i16le(const std::vector<std::uint8_t>& d, std::size_t p) {
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(d[p]) |
        (static_cast<std::uint16_t>(d[p + 1]) << 8));
}

std::uint16_t read_u16le(const std::vector<std::uint8_t>& d, std::size_t p) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(d[p]) |
        (static_cast<std::uint16_t>(d[p + 1]) << 8));
}

}  // namespace

DurLevel parse_dur(const std::vector<std::uint8_t>& data) {
    if (data.size() != kEntryCount * kEntryStride) {
        throw DurError("DUR table must be exactly 960 bytes");
    }
    DurLevel level;
    level.tiles.reserve(kEntryCount);

    for (std::size_t e = 0; e < kEntryCount; ++e) {
        TileCollision tile;
        tile.index = static_cast<int>(e);
        for (std::size_t s = 0; s < kMaxSegments; ++s) {
            const std::size_t off = e * kEntryStride + s * 6;
            const std::int16_t dx = read_i16le(data, off);
            if (dx == -1) break;  // sentinel
            const std::int16_t dy = read_i16le(data, off + 2);
            const std::uint16_t width = read_u16le(data, off + 4);
            if (dx == 0 && dy == 0 && width == 0) break;  // no-op triple
            tile.segments.push_back({dx, dy, width});
        }
        level.tiles.push_back(std::move(tile));
    }
    return level;
}

}  // namespace olduvai::formats
