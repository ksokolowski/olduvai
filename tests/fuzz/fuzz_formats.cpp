// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Multi-format parser fuzzer (clang libFuzzer) — pre-emptive hardening for
// the public release: users will feed corrupt / truncated / foreign-release
// game files, and the parsers must reject them with an exception, never with
// a crash, overflow or unbounded allocation.
//
// One binary covers every format: the first input byte selects the parser,
// the rest is the payload (coverage feedback keeps the corpus per-parser).
// A thrown formats exception = correct rejection; ASan/UBSan violations and
// crashes = findings.
//
// Build:  cmake --preset fuzz
//         cmake --build --preset fuzz --target fuzz_formats
// Run:    ./build/fuzz/tests/fuzz_formats -max_total_time=300 -rss_limit_mb=2048 \
//             -malloc_limit_mb=512 <corpus_dir>
// Corpus dirs are LOCAL ONLY (they accumulate game-derived bytes; the content
// policy forbids committing those).

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

#include "formats/cur.hpp"
#include "formats/dur.hpp"
#include "formats/lzss.hpp"
#include "formats/mat.hpp"
#include "formats/mdi.hpp"
#include "formats/pc1.hpp"
#include "formats/packbits.hpp"
#include "formats/unsqz.hpp"
#include "formats/voc.hpp"
#include "presentation/midi_seq.hpp"

using namespace olduvai;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    if (size < 2) return 0;
    const std::uint8_t selector = data[0] % 9;
    const std::vector<std::uint8_t> p(data + 1, data + size);
    try {
        switch (selector) {
            case 0:
                formats::lzss_decompress(p);
                break;
            case 1:
                formats::unsqz(p);
                break;
            case 2:
                // expected_size is caller-supplied in real use (from container
                // metadata); a fixed cap keeps allocations bounded here.
                formats::packbits_decompress(p, 64 * 1024);
                break;
            case 3:
                formats::parse_pc1(p);
                break;
            case 4: {
                formats::MatFile mat(p, "FUZZ.MAT");
                const auto& sprites = mat.sprites();
                for (std::size_t i = 0; i < sprites.size() && i < 4; ++i)
                    (void)sprites[i].decode_indexed();
                break;
            }
            case 5: {
                formats::CurArchive ar(p);
                const auto& es = ar.entries();
                for (std::size_t i = 0; i < es.size() && i < 4; ++i)
                    (void)ar.get(es[i].name);
                break;
            }
            case 6:
                formats::parse_voc(p);
                break;
            case 7:
                formats::parse_dur(p);
                break;
            case 8: {
                // The GM conversion, then the sequencer over its own output —
                // the exact runtime pipeline for a hostile .MDI.
                const auto midi = formats::build_gm_midi(
                    p, /*track_id=*/1, /*drop_runtime_modulation=*/false,
                    /*gm_translation=*/true);
                presentation::MidiSequencer seq;
                seq.load(midi);
                break;
            }
        }
    } catch (const std::exception&) {
        // Malformed input rejected cleanly — the contract.
    }
    return 0;
}
