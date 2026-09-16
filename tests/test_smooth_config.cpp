// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The smooth-present resolution rule: env > config key > refresh-derived.
// The env vars keep their pre-config behaviour exactly; the config keys
// (smooth_subframes / smooth_vsync) are what a profile or play.json ships.

#include "doctest/doctest.h"

#include "presentation/render/smooth_config.hpp"

using namespace olduvai::presentation;

TEST_CASE("smooth_config: env beats config beats derived") {
    CHECK(resolve_subframe_count(nullptr, 0, 5) == 5);
    CHECK(resolve_subframe_count(nullptr, 2, 5) == 2);
    CHECK(resolve_subframe_count("3", 2, 5) == 3);
}

TEST_CASE("smooth_config: a set but invalid env keeps derived, exactly as before config") {
    CHECK(resolve_subframe_count("abc", 2, 5) == 5);
    CHECK(resolve_subframe_count("13", 2, 5) == 5);
    CHECK(resolve_subframes_explicit("abc", 0));
}

TEST_CASE("smooth_config: only an asked-for count is explicit (d62d922)") {
    CHECK_FALSE(resolve_subframes_explicit(nullptr, 0));
    CHECK(resolve_subframes_explicit(nullptr, 2));
    CHECK(resolve_subframes_explicit("4", 0));
}

TEST_CASE("smooth_config: vsync off from config, or from any env value") {
    CHECK_FALSE(resolve_vsync_off(nullptr, false));
    CHECK(resolve_vsync_off(nullptr, true));
    CHECK(resolve_vsync_off("0", false));
}

// smooth_vsync=off on KMSDRM means asynchronous page flips, and a driver that
// cannot do them rejects every flip (Powkiddy A12 / RK3128, 2026-09-13: 1747
// "Could not queue pageflip: -22" in 260 ticks with off, 0 with auto, panel
// verified on for both).  The CONFIG knob stops there; the env var is a
// deliberate debug override and still forces the fallback.
TEST_CASE("smooth_config: config vsync-off does not apply on kmsdrm") {
    CHECK_FALSE(resolve_vsync_off(nullptr, true, "kmsdrm"));
    CHECK(resolve_vsync_off(nullptr, true, "mali"));      // TrimUI: measured best
    CHECK(resolve_vsync_off(nullptr, true, "cocoa"));
    CHECK(resolve_vsync_off(nullptr, true, nullptr));     // no video yet
    CHECK(resolve_vsync_off("1", false, "kmsdrm"));       // env still wins
    CHECK_FALSE(resolve_vsync_off(nullptr, false, "kmsdrm"));
}

TEST_CASE("smooth_config: parsing accepts the documented values only") {
    int n = 7;
    CHECK(parse_smooth_subframes("0", n));
    CHECK(n == 0);
    CHECK(parse_smooth_subframes("12", n));
    CHECK(n == 12);
    n = 7;
    CHECK_FALSE(parse_smooth_subframes("13", n));
    CHECK_FALSE(parse_smooth_subframes("-1", n));
    CHECK_FALSE(parse_smooth_subframes("two", n));
    CHECK(n == 7);
    bool off = true;
    CHECK(parse_smooth_vsync("auto", off));
    CHECK_FALSE(off);
    CHECK(parse_smooth_vsync("off", off));
    CHECK(off);
    CHECK_FALSE(parse_smooth_vsync("on", off));
}

TEST_CASE("smooth_config: apply_smooth_key folds only its own keys") {
    SmoothPresentConfig c;
    CHECK(apply_smooth_key(c, "smooth_subframes", "2"));
    CHECK(c.subframes == 2);
    CHECK(apply_smooth_key(c, "smooth_vsync", "off"));
    CHECK(c.vsync_off);
    CHECK(apply_smooth_key(c, "smooth_subframes", "junk"));   // ours, kept
    CHECK(c.subframes == 2);
    CHECK_FALSE(apply_smooth_key(c, "hd_profile", "smooth"));
}
