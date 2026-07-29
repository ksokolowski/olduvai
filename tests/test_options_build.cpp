// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// The validate-and-assemble half of the main() decomposition (audit A2):
// build_game_options turns resolved CliArgs + PlaySettings into a GameOptions.
// These cases pin the exact exit codes / stderr strings and the cross-field
// derivations with real 0.9.2 regression history — the reason the block was
// lifted out of main() where it could not be tested.

#include "doctest/doctest.h"

#include "app/options_build.hpp"

using olduvai::app::BuildOutcome;
using olduvai::app::build_game_options;
using olduvai::app::CliArgs;
using olduvai::app::PlaySettings;
using olduvai::presentation::GameOptions;

TEST_CASE("options_build: defaults produce a valid GameOptions") {
    CliArgs a;
    PlaySettings s;   // display_mode gpu, transitions smooth, aspect keep, ...
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    REQUIRE(bo.ok);
    CHECK(bo.exit_code == 0);
    CHECK(bo.warnings.empty());
    // Empty hd_profile resolves to the historical omniscale default.
    CHECK(go.hd_profile == "omniscale");
    // Friendly hd-font name maps to the bundled TTF.
    CHECK(go.hd_font == "FreckleFace-Regular.ttf");
    CHECK(go.enhanced == false);
    CHECK(go.transitions == "smooth");
    CHECK(bool(go.persist));   // the persist closure is wired
}

// --enhanced is all-or-nothing now.  A legacy list no longer selects
// features; it only says "this user wanted enhanced mode".  That rule is what
// keeps a play.json written by the old Options menu — which persisted subsets
// as enhanced=false PLUS a granular list — from silently booting into DOS.
TEST_CASE("options_build: a legacy --enhance list means enhanced, not a subset") {
    CliArgs a;
    PlaySettings s;
    s.enhance_list = "hud-overlay, fluid-bubbles";
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    REQUIRE(bo.ok);
    CHECK(go.enhanced == true);
    // smooth_motion follows the umbrella; it is derived, not listed.
    CHECK(go.enhance.smooth_motion == true);
}

TEST_CASE("options_build: enhanced=false plus a legacy list still means enhanced") {
    CliArgs a;
    PlaySettings s;
    s.enhanced = false;                  // exactly what the old menu persisted
    s.enhance_list = "hd-text,smooth-motion";
    s.enhance_list_from_config = true;
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    REQUIRE(bo.ok);
    CHECK(go.enhanced == true);
}

TEST_CASE("options_build: unknown --enhance feature is exit 2") {
    CliArgs a;
    PlaySettings s;
    s.enhance_list = "hud-overlay,bogus-feature";
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    CHECK_FALSE(bo.ok);
    CHECK(bo.exit_code == 2);
    CHECK(bo.error ==
          "olduvai: unknown --enhance feature 'bogus-feature'.  Known: "
          "smooth-motion, cinematic-cue, hud-overlay, fluid-bubbles, "
          "secret-slide, descent-pan, hd-text\n");
}

// The SAME unknown name must not be fatal when it arrived from play.json.
// The Options menu writes that key itself, so a token this build no longer
// knows is the program's own doing, not a typo — rejecting it would leave the
// user with a game that will not start, quoting a flag they never typed.
TEST_CASE("options_build: unknown enhance feature FROM CONFIG warns, not exits") {
    CliArgs a;
    PlaySettings s;
    s.enhance_list = "hud-overlay,bogus-feature";
    s.enhance_list_from_config = true;
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    CHECK(bo.ok);
    CHECK(bo.exit_code == 0);
    CHECK(bo.error.empty());
    REQUIRE(bo.warnings.size() >= 1);
    CHECK(bo.warnings.back() ==
          "olduvai: ignoring unknown enhance feature 'bogus-feature' "
          "from the config file\n");
    // The recognised name still counts as "wanted enhanced".
    CHECK(go.enhanced);
}

TEST_CASE("options_build: tuning-flag typos are each exit 2") {
    SUBCASE("display-mode") {
        CliArgs a; PlaySettings s; s.display_mode = "vulkan"; GameOptions go;
        const BuildOutcome bo = build_game_options(a, s, go);
        CHECK_FALSE(bo.ok);
        CHECK(bo.exit_code == 2);
        CHECK(bo.error ==
              "olduvai: --display-mode must be 'gpu' or 'cpu' (got 'vulkan')\n");
    }
    SUBCASE("transitions") {
        CliArgs a; PlaySettings s; s.transitions = "fancy"; GameOptions go;
        const BuildOutcome bo = build_game_options(a, s, go);
        CHECK_FALSE(bo.ok);
        CHECK(bo.exit_code == 2);
    }
    SUBCASE("aspect") {
        CliArgs a; PlaySettings s; s.aspect = "cinemascope"; GameOptions go;
        const BuildOutcome bo = build_game_options(a, s, go);
        CHECK_FALSE(bo.ok);
        CHECK(bo.exit_code == 2);
    }
    SUBCASE("hd-font") {
        CliArgs a; PlaySettings s; s.hd_font = "comic"; GameOptions go;
        const BuildOutcome bo = build_game_options(a, s, go);
        CHECK_FALSE(bo.ok);
        CHECK(bo.exit_code == 2);
    }
    SUBCASE("banner-fx") {
        CliArgs a; PlaySettings s; s.banner_fx = "disco"; GameOptions go;
        const BuildOutcome bo = build_game_options(a, s, go);
        CHECK_FALSE(bo.ok);
        CHECK(bo.exit_code == 2);
    }
}

TEST_CASE("options_build: unknown --hd-profile is exit 2 and lists the set") {
    CliArgs a;
    PlaySettings s;
    s.hd_profile = "hqx";
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    CHECK_FALSE(bo.ok);
    CHECK(bo.exit_code == 2);
    CHECK(bo.error.find("--hd-profile 'hqx' is not supported") !=
          std::string::npos);
    CHECK(bo.error.find("omniscale") != std::string::npos);   // the list
}

TEST_CASE("options_build: --transitions classic forces smooth-motion off") {
    CliArgs a;
    PlaySettings s;
    s.enhance_list = "smooth-motion";
    s.transitions = "classic";
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    REQUIRE(bo.ok);
    CHECK(go.enhance.smooth_motion == false);
    CHECK(bo.warnings.empty());   // classic override is silent
}

TEST_CASE("options_build: --trace forces classic and warns when smooth was on") {
    CliArgs a;
    a.play_trace = "run.jsonl";
    PlaySettings s;
    s.enhance_list = "smooth-motion";
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    REQUIRE(bo.ok);
    CHECK(go.enhance.smooth_motion == false);
    REQUIRE(bo.warnings.size() == 1);
    CHECK(bo.warnings[0].find("--trace") != std::string::npos);
    CHECK(bo.warnings[0].find("classic") != std::string::npos);
    // The MODE must change, not just the flag.  smooth_motion is derived from
    // `enhanced`, and a re-init re-derives it — if --trace only cleared the
    // flag, a Style change mid-trace would switch smooth motion back on and
    // the frame count would stop being deterministic.
    CHECK(go.transitions == "classic");
}

TEST_CASE("options_build: widescreen without the HD substrate warns, not fails") {
    CliArgs a;
    PlaySettings s;
    s.aspect = "widescreen";   // but enhanced stays false => no HD substrate
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    REQUIRE(bo.ok);            // non-fatal — falls back to pillarbox
    REQUIRE(bo.warnings.size() == 1);
    CHECK(bo.warnings[0].find("widescreen needs the enhanced HD substrate") !=
          std::string::npos);
}

TEST_CASE("options_build: vga_scan forces vsync only for CLASSIC (0.9.2 bug)") {
    SUBCASE("classic / non-enhanced: vga_scan implies vsync") {
        CliArgs a; PlaySettings s;
        s.vga_scan = true;   // default; enhanced false => no HD substrate
        GameOptions go;
        const BuildOutcome bo = build_game_options(a, s, go);
        REQUIRE(bo.ok);
        CHECK(go.vsync == true);
    }
    SUBCASE("enhanced HD: vga_scan must NOT force vsync") {
        CliArgs a; PlaySettings s;
        s.vga_scan = true;
        s.enhance_list = "hud-overlay";   // => enhanced, omniscale (non-native)
        GameOptions go;
        const BuildOutcome bo = build_game_options(a, s, go);
        REQUIRE(bo.ok);
        CHECK(go.vsync == false);   // the HD substrate must not be vsync-forced
    }
}
