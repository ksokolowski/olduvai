// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "formats/mat.hpp"

#include <algorithm>
#include <cctype>

#include "formats/packbits.hpp"

namespace olduvai::formats {

namespace {

std::uint16_t read_u16be(const std::vector<std::uint8_t>& d, std::size_t p) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(d[p]) << 8) | d[p + 1]);
}

bool is_monochrome_name(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return upper.find("CHARSET1") != std::string::npos;
}

}  // namespace

std::vector<IndexedPixel> Sprite::decode_indexed() const {
    const std::size_t w = width, h = height;
    const std::size_t groups = (w + 7) / 8;
    std::vector<IndexedPixel> out(w * h);

    auto byte_at = [&](std::size_t off) -> std::uint8_t {
        return off < raw_pixels.size() ? raw_pixels[off] : 0;
    };

    if (format == SpriteFormat::Mono1bpp) {
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const std::uint8_t b = byte_at(y * groups + x / 8);
                if ((b >> (7 - (x % 8))) & 1) {
                    out[y * w + x] = {15, true};
                }
            }
        }
        return out;
    }

    // 5-plane, non-interleaved: plane blocks of groups*h bytes each.
    const std::size_t plane = groups * h;
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t gx = 0; gx < groups; ++gx) {
            const std::size_t row_off = y * groups + gx;
            const std::uint8_t b0 = byte_at(row_off);
            const std::uint8_t b1 = byte_at(plane + row_off);
            const std::uint8_t b2 = byte_at(plane * 2 + row_off);
            const std::uint8_t b3 = byte_at(plane * 3 + row_off);
            const std::uint8_t mb = byte_at(plane * 4 + row_off);
            for (std::size_t bit = 0; bit < 8; ++bit) {
                const std::size_t x = gx * 8 + bit;
                if (x >= w) break;
                const int shift = static_cast<int>(7 - bit);
                if (!((mb >> shift) & 1)) continue;  // transparent
                const std::uint8_t color = static_cast<std::uint8_t>(
                    ((b0 >> shift) & 1) |
                    (((b1 >> shift) & 1) << 1) |
                    (((b2 >> shift) & 1) << 2) |
                    (((b3 >> shift) & 1) << 3));
                out[y * w + x] = {color, true};
            }
        }
    }
    return out;
}

MatFile::MatFile(const std::vector<std::uint8_t>& data,
                 const std::string& name) {
    if (data.size() < 2) return;
    const std::uint16_t count = read_u16be(data, 0);
    const bool monochrome = is_monochrome_name(name);
    std::size_t pos = 2;

    for (std::uint16_t i = 0; i < count; ++i) {
        if (pos + 6 > data.size()) break;
        Sprite s;
        s.width = read_u16be(data, pos);
        s.height = read_u16be(data, pos + 2);
        const std::uint16_t isize = read_u16be(data, pos + 4);
        pos += 6;

        if (s.width == 0 || s.height == 0 ||
            s.width > 2048 || s.height > 2048) {
            break;
        }
        const std::size_t groups = (s.width + 7) / 8;

        if (monochrome) {
            const std::size_t raw_size = groups * s.height;
            s.format = SpriteFormat::Mono1bpp;
            s.raw_pixels.assign(
                data.begin() + static_cast<long>(std::min(pos, data.size())),
                data.begin() + static_cast<long>(std::min(pos + raw_size, data.size())));
            pos += raw_size;
        } else if (isize == 0) {
            const std::size_t raw_size = groups * 5 * s.height;
            s.format = SpriteFormat::Ilbm5;
            s.raw_pixels.assign(
                data.begin() + static_cast<long>(std::min(pos, data.size())),
                data.begin() + static_cast<long>(std::min(pos + raw_size, data.size())));
            pos += raw_size;
        } else {
            const std::size_t end = std::min(pos + isize, data.size());
            const std::vector<std::uint8_t> compressed(
                data.begin() + static_cast<long>(std::min(pos, data.size())),
                data.begin() + static_cast<long>(end));
            pos += isize;
            s.format = SpriteFormat::Ilbm5;
            s.raw_pixels = packbits_decompress(compressed,
                                               groups * 5 * s.height);
        }
        sprites_.push_back(std::move(s));
    }
}

}  // namespace olduvai::formats
