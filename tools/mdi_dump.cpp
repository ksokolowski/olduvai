// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Dump olduvai's build_gm_midi() output for an MDI in a .CUR/.VGA archive, so
// it can be A/B'd against the Python reference (to_gm_midi) and rendered
// through libmt32emu.  Used to verify the mt32 music conversion.
//
//   mdi_dump <game_dir> <MDI_NAME> <track_id> <mt32_strict 0|1> <gm_translate 0|1> > out.mid
//
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "formats/cur.hpp"
#include "formats/mdi.hpp"

static std::vector<std::uint8_t> slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: mdi_dump <game_dir> <MDI_NAME> <track_id> "
                             "<mt32_strict 0|1> <gm_translate 0|1>\n");
        return 2;
    }
    const std::string dir = argv[1], name = argv[2];
    const int track_id = std::atoi(argv[3]);
    const bool mt32_strict = std::atoi(argv[4]) != 0;
    const bool gm_translate = std::atoi(argv[5]) != 0;

    const std::vector<std::uint8_t>* md = nullptr;
    olduvai::formats::CurArchive fa(slurp(dir + "/FILESA.CUR"));
    olduvai::formats::CurArchive fb(slurp(dir + "/FILESB.CUR"));
    if (fa.contains(name)) md = &fa.get(name).data;
    else if (fb.contains(name)) md = &fb.get(name).data;
    if (md == nullptr) {
        std::fprintf(stderr, "mdi_dump: %s not found in archives\n", name.c_str());
        return 1;
    }
    const auto out =
        olduvai::formats::build_gm_midi(*md, track_id, mt32_strict, gm_translate);
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
