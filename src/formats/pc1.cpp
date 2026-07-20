// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "formats/pc1.hpp"

#include <cmath>
#include <cstring>

#include "formats/packbits.hpp"

namespace olduvai::formats {

namespace {

std::uint32_t read_u32be(const std::vector<std::uint8_t>& d, std::size_t p) {
    return (static_cast<std::uint32_t>(d[p]) << 24) |
           (static_cast<std::uint32_t>(d[p + 1]) << 16) |
           (static_cast<std::uint32_t>(d[p + 2]) << 8) |
           static_cast<std::uint32_t>(d[p + 3]);
}

std::uint16_t read_u16be(const std::vector<std::uint8_t>& d, std::size_t p) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(d[p]) << 8) | d[p + 1]);
}

bool tag_is(const std::vector<std::uint8_t>& d, std::size_t p, const char* t) {
    return p + 4 <= d.size() && std::memcmp(d.data() + p, t, 4) == 0;
}

// Interleaved planar rows → one palette index byte per pixel.
std::vector<std::uint8_t> decode_planes(const std::vector<std::uint8_t>& raw,
                                        std::size_t width, std::size_t height,
                                        std::size_t nplanes) {
    const std::size_t groups = (width + 7) / 8;
    const std::size_t stride = groups * nplanes;
    std::vector<std::uint8_t> pixels(width * height, 0);
    for (std::size_t y = 0; y < height; ++y) {
        const std::size_t row = y * stride;
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t byte_idx = x >> 3;
            const int shift = 7 - static_cast<int>(x & 7);
            std::uint8_t idx = 0;
            for (std::size_t p = 0; p < nplanes; ++p) {
                const std::size_t off = row + p * groups + byte_idx;
                const std::uint8_t b = off < raw.size() ? raw[off] : 0;
                idx = static_cast<std::uint8_t>(idx | (((b >> shift) & 1) << p));
            }
            pixels[y * width + x] = idx;
        }
    }
    return pixels;
}

}  // namespace

Pc1Image parse_pc1(const std::vector<std::uint8_t>& data) {
    if (data.size() < 12 || !tag_is(data, 0, "FORM")) {
        throw Pc1Error("not an IFF file");
    }
    if (!tag_is(data, 8, "ILBM")) {
        throw Pc1Error("expected ILBM subtype");
    }

    Pc1Image img;
    std::uint8_t nplanes = 0, compression = 0;
    std::vector<std::uint8_t> body;
    bool have_body = false;

    std::size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const std::size_t csize = read_u32be(data, pos + 4);
        const std::size_t cstart = pos + 8;
        const std::size_t cend = std::min(cstart + csize, data.size());

        if (tag_is(data, pos, "BMHD")) {
            // Validate against the bytes actually present (cend), not the
            // declared csize — a file truncated mid-BMHD must not read past
            // the buffer end.
            if (csize < 20 || cend - cstart < 11)
                throw Pc1Error("BMHD too small or truncated");
            img.width = read_u16be(data, cstart);
            img.height = read_u16be(data, cstart + 2);
            nplanes = data[cstart + 8];
            compression = data[cstart + 10];
        } else if (tag_is(data, pos, "CMAP")) {
            // Iterate over present bytes, not the declared csize: a CMAP
            // whose header over-declares its size (truncated file) must not
            // index past the buffer or drive a giant allocation.
            const std::size_t n = (cend - cstart) / 3;
            img.palette.clear();
            for (std::size_t i = 0; i < n; ++i) {
                // 6-bit DAC precision (v >> 2), scaled back to 8-bit.
                const auto to8 = [](std::uint8_t v) {
                    const int v6 = v >> 2;
                    return static_cast<std::uint8_t>(
                        std::lround(v6 * 255.0 / 63.0));
                };
                img.palette.push_back({to8(data[cstart + i * 3]),
                                       to8(data[cstart + i * 3 + 1]),
                                       to8(data[cstart + i * 3 + 2])});
            }
        } else if (tag_is(data, pos, "BODY")) {
            body.assign(data.begin() + static_cast<long>(cstart),
                        data.begin() + static_cast<long>(cend));
            have_body = true;
        }
        pos = cstart + csize;
        if (csize % 2) ++pos;  // IFF pads chunks to even length
    }

    if (img.palette.empty()) throw Pc1Error("no CMAP chunk");
    if (!have_body) throw Pc1Error("no BODY chunk");
    if (img.width == 0 || img.height == 0 || nplanes == 0) {
        throw Pc1Error("BMHD missing or incomplete");
    }
    // Each pixel decodes to a single palette-index byte, so the plane bits are
    // shifted into positions 0..nplanes-1: more than 8 planes both overflows
    // the index and shifts an int past its width (UB). No real ILBM/PC1 uses
    // >8 planes for indexed colour; a hostile header (nplanes up to 255) is
    // malformed (fuzz_formats: shift-out-of-bounds in decode_planes).
    if (nplanes > 8) throw Pc1Error("too many bitplanes");
    // width/height are 16-bit header fields (up to 65535 each): a hostile BMHD
    // can demand a multi-gigabyte pixel/plane buffer from a tiny file (the raw
    // plane buffer is zero-padded to the declared size regardless of BODY
    // length). Prehistorik PC1 images are VGA screen-sized (320x200-class);
    // cap the area far above any real asset but well below an OOM
    // (fuzz_formats: 65535x65506 forced a ~4 GB allocation). Computed in
    // size_t — the int-promoted product would overflow.
    constexpr std::size_t kMaxPc1Pixels = 16u * 1024 * 1024;  // 16 Mpx
    if (static_cast<std::size_t>(img.width) * img.height > kMaxPc1Pixels) {
        throw Pc1Error("image dimensions too large");
    }

    const std::size_t groups = (img.width + 7) / 8;
    const std::size_t expected = static_cast<std::size_t>(img.height) *
                                 nplanes * groups;
    std::vector<std::uint8_t> raw;
    if (compression == 1) {
        raw = packbits_decompress(body, expected);
    } else {
        raw = body;
        raw.resize(expected, 0);
    }
    img.pixels = decode_planes(raw, img.width, img.height, nplanes);
    return img;
}

}  // namespace olduvai::formats
