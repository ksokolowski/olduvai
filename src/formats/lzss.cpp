// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "formats/lzss.hpp"

namespace olduvai::formats {

namespace {

// MSB-first bit reader over a byte span.  Reads past the end yield 0,
// matching the reference implementation (trailing padding bits).
class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    int read_bit() {
        if (bits_left_ == 0) {
            if (pos_ >= size_) return 0;
            buf_ = data_[pos_++];
            bits_left_ = 8;
        }
        --bits_left_;
        return (buf_ >> bits_left_) & 1;
    }

    std::uint32_t read_bits(int n) {
        std::uint32_t value = 0;
        for (int i = 0; i < n; ++i) {
            value = (value << 1) | static_cast<std::uint32_t>(read_bit());
        }
        return value;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::uint8_t buf_ = 0;
    int bits_left_ = 0;
};

}  // namespace

std::vector<std::uint8_t> lzss_decompress(const std::uint8_t* data,
                                          std::size_t size) {
    if (size < 4) {
        throw LzssError("data too short to contain LZSS size header");
    }
    const std::uint32_t expected =
        (static_cast<std::uint32_t>(data[0]) << 24) |
        (static_cast<std::uint32_t>(data[1]) << 16) |
        (static_cast<std::uint32_t>(data[2]) << 8) |
        static_cast<std::uint32_t>(data[3]);
    // Sanity cap: the header is attacker/corruption-controlled and drives the
    // reserve + fill loop (exhausted input degenerates to literal zeros, so a
    // 0xFFFFFFFF header means a 4 GiB allocation).  Real assets are < 1 MiB;
    // 9 bits of input per output literal bounds any genuine stream.
    constexpr std::uint32_t kMaxOutput = 16u * 1024u * 1024u;
    if (expected > kMaxOutput) {
        throw LzssError("LZSS header declares implausible output size");
    }
    // ...and bounded by the INPUT, not only by a constant.  The densest
    // token is a back-reference: 11 bits for at most 5 bytes, so a genuine
    // stream cannot declare more than (size-4)*8*5/11 bytes; the slack covers
    // the zero bits an exhausted reader supplies past the final byte.
    // Without this a 34-byte input asked for 16 MiB of zero-fill, one bit at
    // a time — 0.7 s per input under ASan, which starved the fuzzer (it fell
    // to 13 exec/s) and is the same stall for a corrupt game file.
    const std::uint64_t max_from_input =
        (static_cast<std::uint64_t>(size - 4) * 8u * 5u) / 11u + 64u;
    if (expected > max_from_input) {
        throw LzssError("LZSS header declares more output than the input can encode");
    }

    BitReader bits(data + 4, size - 4);

    std::vector<std::uint8_t> output;
    output.reserve(expected);
    std::uint8_t window[256] = {0};  // pre-filled with 0x00
    unsigned write_pos = 0;

    while (output.size() < expected) {
        if (bits.read_bit() == 0) {
            // Literal byte.
            const auto byte = static_cast<std::uint8_t>(bits.read_bits(8));
            output.push_back(byte);
            window[write_pos] = byte;
            write_pos = (write_pos + 1) & 0xFF;
        } else {
            // Back-reference: 2-bit length code, 8-bit distance code.
            const unsigned length = 2 + bits.read_bits(2);     // 2..5
            const unsigned distance = 1 + bits.read_bits(8);   // 1..256
            unsigned read_pos = (write_pos - distance) & 0xFF;
            for (unsigned i = 0; i < length; ++i) {
                if (output.size() >= expected) break;
                const std::uint8_t byte = window[read_pos];
                output.push_back(byte);
                window[write_pos] = byte;
                write_pos = (write_pos + 1) & 0xFF;
                read_pos = (read_pos + 1) & 0xFF;
            }
        }
    }

    if (output.size() != expected) {
        throw LzssError("decompression size mismatch");
    }
    return output;
}

}  // namespace olduvai::formats
