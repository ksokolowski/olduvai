// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// AdLib/OPL SFX renderer: structural fidelity guards.  Byte-equivalence with
// the Python reference implementation is verified out-of-band by PCM diff
// — both drive the same Nuked-OPL3 core with the same register stream.  These
// tests lock the deterministic, framework-checkable properties: catalog
// presence, output length (gate+tail at the sample rate), non-silence, and
// reproducibility.

#include <cstdlib>
#include <vector>

#include "doctest/doctest.h"
#include "prepare/exe_tables.hpp"
#include "presentation/opl_sfx.hpp"

using namespace olduvai::presentation;

namespace {

int peak_abs(const std::vector<std::int16_t>& pcm) {
    int p = 0;
    for (std::int16_t s : pcm) p = std::max(p, std::abs(static_cast<int>(s)));
    return p;
}

// The real voice patches are game data read from the user's executable at
// startup; the tests install a synthetic audible patch (full-volume
// carrier: attack 15, sustained EG, TL-base 0) so they stay data-free.
void install_test_voices() {
    olduvai::prepare::AdlibSfxVoice v;
    //        KSL Mult -  AR SL EG DR RR TL AM Vib KSR FB
    v.mod = {{0,  1,  0, 15, 0, 1, 0, 7, 63, 0, 0,  0,  1}};
    v.car = {{0,  1,  0, 15, 0, 1, 0, 7, 0,  0, 0,  0,  1}};
    v.mod_wf = 0;
    v.car_wf = 0;
    olduvai::prepare::AdlibSfxVoices set;
    set.generic = v;
    set.jump_apex = v;
    set.hit = v;
    install_adlib_sfx_voices(set);
}

}  // namespace

TEST_CASE("opl_sfx catalog holds the three AdLib SFX once voices install") {
    install_test_voices();
    CHECK(opl_sfx_lookup("SFX_HIT") != nullptr);
    CHECK(opl_sfx_lookup("SFX_JUMP_APEX") != nullptr);
    CHECK(opl_sfx_lookup("SFX_GENERIC") != nullptr);
    CHECK(opl_sfx_lookup("SFX_NOPE") == nullptr);
    CHECK(opl_sfx_ids().size() == 3);
}

TEST_CASE("opl_sfx renders gate+tail frames at the sample rate") {
    install_test_voices();
    const int rate = 44100;
    const OplSfxDef* d = opl_sfx_lookup("SFX_HIT");
    REQUIRE(d != nullptr);
    auto pcm = render_adlib_sfx_by_id("SFX_HIT", rate);
    // gate 800ms + tail 400ms = 1200ms → 52920 stereo frames → 105840 samples.
    const long gate = static_cast<long>(rate * d->gate_ms / 1000.0);
    const long tail = static_cast<long>(rate * d->tail_ms / 1000.0);
    CHECK(pcm.size() == static_cast<std::size_t>((gate + tail) * 2));
}

TEST_CASE("opl_sfx output is non-silent for each SFX") {
    install_test_voices();
    for (const char* id : {"SFX_HIT", "SFX_JUMP_APEX", "SFX_GENERIC"}) {
        auto pcm = render_adlib_sfx_by_id(id, 44100);
        REQUIRE(!pcm.empty());
        CHECK(peak_abs(pcm) > 100);   // FM voice actually sounds
    }
}

TEST_CASE("opl_sfx render is deterministic") {
    install_test_voices();
    auto a = render_adlib_sfx_by_id("SFX_GENERIC", 48000);
    auto b = render_adlib_sfx_by_id("SFX_GENERIC", 48000);
    CHECK(a == b);
}

TEST_CASE("opl_sfx rejects bad input / unknown id") {
    CHECK(render_adlib_sfx_by_id("SFX_NOPE", 44100).empty());
    const OplSfxVoice z{};
    CHECK(render_adlib_sfx(z, z, /*mwf*/4, 0, 36, 127, 3, 0, 44100, 100, 100)
              .empty());                                    // waveform > 3
    CHECK(render_adlib_sfx(z, z, 0, 0, 36, 127, 3, 0, /*rate*/0, 100, 100)
              .empty());                                    // bad rate
}
