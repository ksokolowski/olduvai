// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Minimal 16-bit PCM WAV read/write.  Written for the HD SFX bake pipeline,
// which is gone; `write_wav16` outlived it as the `--render-audio-out` writer
// the cross-platform audio gate compares (tests/audio_render.sh), and
// `read_wav16` is now exercised only by test_wav_io.cpp — kept as the
// round-trip half that proves the writer.  Only the plain RIFF/fmt/data shape
// this file produces is accepted — not a general WAV parser.
#pragma once

#include <string>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace olduvai::presentation {

// FNV-1a/64 of decoded sample bytes, as 16 lowercase hex digits.  Used by
// --render-audio to give each rendered stream a stable content name, which is
// what the cross-platform audio gate compares (tests/audio_render.sh).  It
// outlived the HD SFX bake it was written for.
inline std::string sfx_digest_hex(const std::vector<std::uint8_t>& d) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const std::uint8_t b : d) {
        h ^= b;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string(buf);
}

inline bool write_wav16(const std::filesystem::path& path,
                        const std::vector<std::int16_t>& pcm, int rate,
                        int channels) {
    if (rate <= 0 || channels <= 0) return false;
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    if (f == nullptr) return false;
    const auto u32 = [&](std::uint32_t v) {
        const unsigned char b[4] = {
            static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
            static_cast<unsigned char>(v >> 16),
            static_cast<unsigned char>(v >> 24)};
        std::fwrite(b, 1, 4, f);
    };
    const auto u16 = [&](std::uint16_t v) {
        const unsigned char b[2] = {static_cast<unsigned char>(v),
                                    static_cast<unsigned char>(v >> 8)};
        std::fwrite(b, 1, 2, f);
    };
    const std::uint32_t data_bytes =
        static_cast<std::uint32_t>(pcm.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + data_bytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16);
    u16(1);   // PCM
    u16(static_cast<std::uint16_t>(channels));
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate) * 2u *
        static_cast<std::uint32_t>(channels));
    u16(static_cast<std::uint16_t>(2 * channels));
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(data_bytes);
    const std::size_t n = std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    return n == pcm.size();
}

// Reads a 16-bit PCM WAV written by write_wav16.  Returns the interleaved
// samples; empty (rate/channels untouched beyond 0) on any structural
// mismatch.
inline std::vector<std::int16_t> read_wav16(const std::filesystem::path& path,
                                            int& rate, int& channels) {
    rate = 0;
    channels = 0;
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (f == nullptr) return {};
    std::vector<unsigned char> raw;
    unsigned char tmp[4096];
    std::size_t got;
    while ((got = std::fread(tmp, 1, sizeof(tmp), f)) > 0) {
        raw.insert(raw.end(), tmp, tmp + got);
    }
    std::fclose(f);
    const auto rd32 = [&](std::size_t o) -> std::uint32_t {
        return raw[o] | (raw[o + 1] << 8) | (raw[o + 2] << 16) |
               (static_cast<std::uint32_t>(raw[o + 3]) << 24);
    };
    const auto rd16 = [&](std::size_t o) -> std::uint32_t {
        return raw[o] | (raw[o + 1] << 8);
    };
    if (raw.size() < 44 || std::memcmp(raw.data(), "RIFF", 4) != 0 ||
        std::memcmp(raw.data() + 8, "WAVE", 4) != 0) {
        return {};
    }
    // Walk chunks: accept fmt (PCM16) + data in any order, skip the rest.
    std::size_t pos = 12;
    int fmt_rate = 0, fmt_channels = 0;
    bool fmt_ok = false;
    std::vector<std::int16_t> pcm;
    while (pos + 8 <= raw.size()) {
        const std::uint32_t len = rd32(pos + 4);
        const std::size_t body = pos + 8;
        if (body + len > raw.size()) return {};
        if (std::memcmp(raw.data() + pos, "fmt ", 4) == 0 && len >= 16) {
            if (rd16(body) != 1 || rd16(body + 14) != 16) return {};  // PCM16
            fmt_channels = static_cast<int>(rd16(body + 2));
            fmt_rate = static_cast<int>(rd32(body + 4));
            fmt_ok = fmt_channels > 0 && fmt_rate > 0;
        } else if (std::memcmp(raw.data() + pos, "data", 4) == 0) {
            pcm.resize(len / 2);
            std::memcpy(pcm.data(), raw.data() + body, (len / 2) * 2);
        }
        pos = body + len + (len & 1);   // chunks are word-aligned
    }
    if (!fmt_ok || pcm.empty()) return {};
    rate = fmt_rate;
    channels = fmt_channels;
    return pcm;
}

}  // namespace olduvai::presentation
