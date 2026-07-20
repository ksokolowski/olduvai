// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Dump an AdLib/OPL SFX as raw interleaved-stereo int16 LE PCM to stdout.
// Used to A/B the native renderer against the Python reference
// (formats/opl_sfx.py): both drive the same Nuked-OPL3 core with the same
// register stream, so the PCM must match byte-for-byte.
//
//   ./opl_sfx_dump SFX_HIT 44100 > /tmp/cpp.pcm
//
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "presentation/opl_sfx.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: opl_sfx_dump <SFX_ID> <sample_rate>\n");
        return 2;
    }
    const std::string id = argv[1];
    const int rate = std::atoi(argv[2]);
    auto pcm = olduvai::presentation::render_adlib_sfx_by_id(id, rate);
    if (pcm.empty()) {
        std::fprintf(stderr, "opl_sfx_dump: unknown SFX or render failed: %s\n",
                     id.c_str());
        return 1;
    }
    std::fwrite(pcm.data(), sizeof(std::int16_t), pcm.size(), stdout);
    return 0;
}
