// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Headless OPL music render — the byte-parity harness for the authentic
// AdLib driver.  Renders a user-supplied music file to WAV exactly like the
// reference renderer's one-shot mode (all-notes-off + 350 ms release tail)
// so the two PCM streams can be byte-compared:
//
//   opl_music_dump <in.mdi> <out.wav> [sample_rate]
//
// Reference side: the reference implementation's OPL WAV renderer.
// The PCM of game music is game content — compare locally, never commit.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "presentation/opl_music.hpp"

namespace {

void put_u32(std::ofstream& f, std::uint32_t v) {
    const char b[4] = {static_cast<char>(v), static_cast<char>(v >> 8),
                       static_cast<char>(v >> 16), static_cast<char>(v >> 24)};
    f.write(b, 4);
}

void put_u16(std::ofstream& f, std::uint16_t v) {
    const char b[2] = {static_cast<char>(v), static_cast<char>(v >> 8)};
    f.write(b, 2);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: opl_music_dump <in.mdi> <out.wav> [rate]\n");
        return 2;
    }
    const int rate = argc > 3 ? std::atoi(argv[3]) : 44100;

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "opl_music_dump: cannot read %s\n", argv[1]);
        return 1;
    }
    std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());

    olduvai::presentation::OplMusicPlayer player(rate);
    player.set_loop(false);
    if (!player.open(raw)) {
        std::fprintf(stderr, "opl_music_dump: not a music container\n");
        return 1;
    }

    // render() reports the frames generated before its zero-fill, so the
    // one-shot stream ends at the exact same frame count as the reference.
    std::vector<std::int16_t> pcm;
    std::vector<std::int16_t> chunk(4096 * 2);
    while (player.active()) {
        const int got = player.render(4096, chunk.data());
        pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + got * 2);
    }

    std::ofstream out(argv[2], std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "opl_music_dump: cannot write %s\n", argv[2]);
        return 1;
    }
    const std::uint32_t data_bytes =
        static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    out.write("RIFF", 4);
    put_u32(out, 36 + data_bytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    put_u32(out, 16);
    put_u16(out, 1);                                    // PCM
    put_u16(out, 2);                                    // stereo
    put_u32(out, static_cast<std::uint32_t>(rate));
    put_u32(out, static_cast<std::uint32_t>(rate) * 4); // byte rate
    put_u16(out, 4);                                    // block align
    put_u16(out, 16);                                   // bits
    out.write("data", 4);
    put_u32(out, data_bytes);
    out.write(reinterpret_cast<const char*>(pcm.data()), data_bytes);
    std::printf("opl_music_dump: %zu frames -> %s\n", pcm.size() / 2, argv[2]);
    return 0;
}
