// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "formats/unsqz.hpp"

#include <array>

namespace olduvai::formats {

namespace {

constexpr int kCodeWidth = 9;                    // initial code size
constexpr int kMaxWidth = 12;                    // dictionary ceiling: 4096
constexpr std::uint16_t kCodeBase = 1u << (kCodeWidth - 1);   // 0x100
constexpr std::uint16_t kClearCode = kCodeBase;               // 0x100
constexpr std::uint16_t kResetCode = kCodeBase + 1;           // 0x101
constexpr std::uint16_t kFirstFree = kCodeBase + 2;           // 0x102
constexpr std::size_t kDictSize = 1u << kMaxWidth;

// MSB-first bit reader.  Reads at least one byte per code (mirroring the
// reference reader), so up to two bytes of over-read slack can remain at
// the end of a stream — harmless: the output size terminates decoding.
struct BitReader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 4;              // past the header
    std::uint32_t bits = 0;
    int bits_left = 0;

    std::uint16_t get(int count) {
        bits = (bits << 8) | next_byte();
        bits_left += 8;
        if (bits_left < count) {
            bits = (bits << 8) | next_byte();
            bits_left += 8;
        }
        const std::uint16_t code =
            static_cast<std::uint16_t>(bits >> (bits_left - count));
        bits_left -= count;
        bits &= (1u << bits_left) - 1u;
        return code;
    }

    std::uint8_t next_byte() {
        if (pos >= size) throw SqzError("sqz: truncated stream");
        return data[pos++];
    }
};

}  // namespace

bool looks_like_sqz(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 5) return false;
    if ((data[1] & 0xF0) != 0x10) return false;
    const std::size_t out_size =
        (static_cast<std::size_t>(data[0] & 0x0F) << 16) |
        static_cast<std::size_t>(data[2]) |
        (static_cast<std::size_t>(data[3]) << 8);
    return out_size > 0;
}

std::vector<std::uint8_t> unsqz(const std::uint8_t* data, std::size_t size) {
    if (!looks_like_sqz(data, size)) throw SqzError("sqz: bad header");
    const std::size_t out_size =
        (static_cast<std::size_t>(data[0] & 0x0F) << 16) |
        static_cast<std::size_t>(data[2]) |
        (static_cast<std::size_t>(data[3]) << 8);

    BitReader in{data, size};
    std::vector<std::uint8_t> out;
    out.reserve(out_size);

    // prefix chain + final byte per dictionary entry; stack unwinds a
    // chain into output order.
    std::array<std::uint16_t, kDictSize> prefix{};
    std::array<std::uint8_t, kDictSize> str{};
    std::array<std::uint8_t, kDictSize> stack{};

    std::uint16_t top_code = 0;   // width grows when next_free reaches this
    int code_size = 0;
    std::uint16_t next_free = 0;
    std::uint16_t prev_code = 0;
    std::uint8_t last_byte = 0;

    auto get_code = [&]() -> std::uint16_t {
        if (top_code == next_free && code_size != kMaxWidth) {
            ++code_size;
            top_code = static_cast<std::uint16_t>(top_code << 1);
        }
        return in.get(code_size);
    };

    // (Re)initialise the dictionary and emit the literal that follows.
    auto reset = [&]() -> std::uint16_t {
        top_code = 1u << kCodeWidth;
        code_size = kCodeWidth;
        next_free = kFirstFree;
        const std::uint16_t code = get_code();
        if (code != kResetCode) {
            prev_code = code;
            last_byte = static_cast<std::uint8_t>(code & 0xFF);
            out.push_back(last_byte);
        }
        return code;
    };

    reset();
    while (out.size() < out_size) {
        std::uint16_t code = get_code();
        if (code == kResetCode || code == kClearCode) {
            reset();
            continue;
        }
        const std::uint16_t current = code;
        std::size_t sp = 0;
        if (code >= next_free) {
            // KwKwK case: code not in the dictionary yet.
            if (code != next_free) throw SqzError("sqz: bad code");
            stack[sp++] = last_byte;
            code = prev_code;
        }
        while (code >= kCodeBase) {
            stack[sp++] = str[code];
            code = prefix[code];
        }
        last_byte = static_cast<std::uint8_t>(code & 0xFF);
        stack[sp++] = last_byte;
        while (sp > 0) out.push_back(stack[--sp]);
        if (next_free < kDictSize) {
            str[next_free] = last_byte;
            prefix[next_free] = prev_code;
            ++next_free;
        }
        prev_code = current;
    }

    if (out.size() != out_size) throw SqzError("sqz: output size mismatch");
    return out;
}

}  // namespace olduvai::formats
