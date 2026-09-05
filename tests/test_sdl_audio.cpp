// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The real SdlAudio device path — the one test that touches the class whose
// callback runs off-main.  SDL_AUDIODRIVER=dummy still opens a device and
// services it from a real audio thread, so play/stop/mix-balance calls from
// the main thread here race the same code a TSan job would watch (BACKLOG
// 4b #12: the thread-sanitizer lane needs this test to exist first; on this
// Mac TSan itself is dead — anything linking sdl2-compat hangs before main).
//
// Always-green: OPL needs no ROMs and no SoundFont, and no game files.
#include "doctest/doctest.h"

#include <cstdint>
#include <thread>
#include <vector>

#include <SDL.h>

#include "presentation/audio/audio.hpp"

namespace {

// FORCE the dummy audio driver.  The header above has always SAID
// "SDL_AUDIODRIVER=dummy", but nothing set it: the value was inherited from
// the environment, and `add_test(NAME sdl_unit ...)` passes none.  On macOS
// that accidentally worked — CoreAudio opens — while the Linux CI container
// has no sound device, SDL_Init(SDL_INIT_AUDIO) fails, and BOTH cases below
// die on their first REQUIRE.  Caught by the 0.9.6 pre-tag dry run, which is
// the first time this test ever ran on Linux.
//
// Applies to the offline case too: offline SdlAudio opens no device, but it
// still initialises the SDL audio subsystem, and that is what was failing.
// Same idiom as test_window_util.cpp's SDL_VIDEODRIVER pin.
void force_dummy_audio() { SDL_setenv("SDL_AUDIODRIVER", "dummy", 1); }

void push_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

// Same synthetic shape as tests/test_opl_music.cpp's builder (kept local: a
// shared fixture header for two call sites would be the only one in the tree).
// Timbre-carrying, because the OPL driver keys nothing without an FF 7F patch.
std::vector<std::uint8_t> synthetic_mdi() {
    std::vector<std::uint8_t> p = {0x00, 0x00, 0x3B, 0x00, 0x01, 0x00};
    const std::uint8_t prim[13] = {1, 2, 3, 10, 4, 1, 5, 6, 16, 1, 0, 1, 0};
    const std::uint8_t sec[13] = {0, 1, 0, 12, 2, 0, 7, 8, 8, 0, 1, 0, 2};
    p.insert(p.end(), prim, prim + 13);
    p.insert(p.end(), sec, sec + 13);
    p.push_back(1);
    p.push_back(2);

    std::vector<std::uint8_t> m = {'M', 'T', 'h', 'd'};
    push_u32(m, 6);
    m.insert(m.end(), {0, 0, 0, 1, 0, 96});
    std::vector<std::uint8_t> t;
    t.push_back(0);
    t.insert(t.end(), {0xFF, 0x7F});
    t.push_back(static_cast<std::uint8_t>(p.size()));
    t.insert(t.end(), p.begin(), p.end());
    t.insert(t.end(), {0, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});
    t.insert(t.end(), {0, 0x90, 60, 127});
    t.insert(t.end(), {96, 0x80, 0, 0});
    t.insert(t.end(), {0, 0xFF, 0x2F, 0x00});
    m.insert(m.end(), {'M', 'T', 'r', 'k'});
    push_u32(m, static_cast<std::uint32_t>(t.size()));
    m.insert(m.end(), t.begin(), t.end());
    return m;
}

}  // namespace

TEST_CASE("SdlAudio: dummy-driver callback thread vs main-thread API") {
    force_dummy_audio();
    olduvai::presentation::SdlAudio audio("opl", "", "", "opl", 48000, 0, "",
                                          /*offline=*/false);
    // The dummy driver opens a real SDL device serviced by a real thread; if
    // a build ever routes this to a null driver with no callback, ok() still
    // holds and the test degenerates to API smoke — acceptable, not fatal.
    REQUIRE(audio.ok());
    CHECK(audio.music_available());
    CHECK(audio.active_music_backend() == "opl");

    const std::vector<std::uint8_t> mdi = synthetic_mdi();
    audio.play_music(mdi, 1);

    // ~600 ms of concurrent access while the callback thread mixes: state
    // polls, mix-balance flips, stop/restart cycles.  Any unsynchronised
    // hand-off between the audio thread and these calls is what a future
    // Linux-only TSan lane exists to catch; this test is what keeps such a
    // lane from being green-for-free.
    for (int i = 0; i < 12; ++i) {
        CHECK(audio.music_available());
        (void)audio.active_music_backend();
        audio.set_mix_balance(i % 2 == 0);
        if (i % 4 == 3) {
            audio.stop_music();
            audio.play_music(mdi, 1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    audio.stop_music();
    // Let the device drain before teardown: the destructor joins the same
    // machinery, and a pending callback at destruction time is the classic
    // shutdown crash the host_midi destructor fix already taught us about.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_CASE("SdlAudio: --render-audio's OPL arm renders non-silent, stable PCM") {
    force_dummy_audio();
    // Offline ctor: no SDL device, no callback — the shipping --render-audio
    // path.  Before render_offline grew its OPL arm (BACKLOG §6), this exact
    // call printed a digest of silence: mix() rendered a player that had
    // never been handed a track.  No pinned hash by policy (std::pow feeds an
    // integer quantisation; FMA contraction under LTO moves it) — run-to-run
    // equality plus a peak threshold instead.
    olduvai::presentation::SdlAudio audio("opl", "", "", "opl", 48000, 0, "",
                                          /*offline=*/true);
    REQUIRE(audio.music_available());
    const int frames = 24000;   // half a second at 48 kHz
    const auto a = audio.render_offline(synthetic_mdi(), frames);
    const auto b = audio.render_offline(synthetic_mdi(), frames);
    REQUIRE(a.size() == static_cast<std::size_t>(frames) * 2);
    REQUIRE(a == b);   // deterministic run-to-run

    int peak = 0;
    for (const std::int16_t s : a) {
        const int m = s < 0 ? -s : s;
        if (m > peak) peak = m;
    }
    CHECK(peak > 1000);   // audibly non-silent: the timbre actually keyed
}

