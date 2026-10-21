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

TEST_CASE("options_build: a saved xbr profile reads as xbrz") {
    CliArgs a;
    PlaySettings s;
    s.hd_profile = "xbr";
    GameOptions go;
    const BuildOutcome bo = build_game_options(a, s, go);
    CHECK(bo.ok);
    CHECK(go.hd_profile == "xbrz");
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

TEST_CASE("options_build: smooth keys reach GameOptions; bad values warn, not exit") {
    CliArgs a;
    PlaySettings s;
    s.smooth_subframes = "2";
    s.smooth_vsync = "off";
    GameOptions go;
    BuildOutcome bo = build_game_options(a, s, go);
    REQUIRE(bo.ok);
    CHECK(go.smooth_subframes == 2);
    CHECK(go.smooth_vsync_off == true);

    PlaySettings t;
    t.smooth_subframes = "99";
    t.smooth_vsync = "sometimes";
    GameOptions go2;
    bo = build_game_options(a, t, go2);
    REQUIRE(bo.ok);                       // a broken config never blocks play
    CHECK(go2.smooth_subframes == 0);     // auto
    CHECK(go2.smooth_vsync_off == false); // auto
    CHECK(bo.warnings.size() == 2);
}

TEST_CASE("options_build: the profile family reaches GameOptions") {
    CliArgs a;
    PlaySettings s;
    s.profile_family = "handheld";
    GameOptions go;
    REQUIRE(build_game_options(a, s, go).ok);
    CHECK(go.profile_family == "handheld");
}

TEST_CASE("options_build: the saved settings reach GameOptions as resolved") {
    CliArgs a;
    PlaySettings s;   // every PlayConfig field off its default
    s.enhanced = true;
    s.hd_profile = "mmpx";
    s.render_scale = 3;
    s.aspect = "4:3";
    s.music_device = "opl";
    s.rom_dir = "/roms";
    s.mt32_model = "cm32l";
    s.soundfont = "/sf/gm.sf2";
    s.sfx_backend = "sb-dac";
    s.autofire = "fast";
    s.pad_jump = "y";
    s.pad_attack = "b";
    s.pad_pause = "back";
    s.pad_confirm = "y";
    s.pad_back = "a";
    s.pad_deadzone = 5000;
    s.key_left = "J";
    s.key_right = "L";
    s.key_up = "I";
    s.key_down = "K";
    s.key_attack = "U";
    s.key_pause = "P";
    s.key_quit = "F9";
    s.display_mode = "cpu";
    s.audio_rate = 44100;
    s.audio_buffer = 1024;
    s.transitions = "classic";
    s.banner_fx = "fire";
    s.profile_family = "handheld";
    GameOptions go;
    REQUIRE(build_game_options(a, s, go).ok);
    CHECK(go.enhanced);
    CHECK(go.hd_profile == "mmpx");
    CHECK(go.render_scale == 3);
    CHECK(go.aspect == "4:3");
    CHECK(go.music_device == "opl");
    CHECK(go.rom_dir == "/roms");
    CHECK(go.mt32_model == "cm32l");
    CHECK(go.soundfont == "/sf/gm.sf2");
    CHECK(go.sfx_backend == "sb-dac");
    CHECK(go.autofire == "fast");
    CHECK(go.pad_jump == "y");
    CHECK(go.pad_attack == "b");
    CHECK(go.pad_pause == "back");
    CHECK(go.pad_confirm == "y");
    CHECK(go.pad_back == "a");
    CHECK(go.pad_deadzone == 5000);
    CHECK(go.key_left == "J");
    CHECK(go.key_right == "L");
    CHECK(go.key_up == "I");
    CHECK(go.key_down == "K");
    CHECK(go.key_attack == "U");
    CHECK(go.key_pause == "P");
    CHECK(go.key_quit == "F9");
    CHECK(go.display_mode == "cpu");
    CHECK(go.audio_rate == 44100);
    CHECK(go.audio_buffer == 1024);
    CHECK(go.transitions == "classic");
    CHECK(go.banner_fx == "fire");
    CHECK(go.profile_family == "handheld");
}

TEST_CASE("options_build: round 1's Nintendo mapping is upgraded on the way") {
    CliArgs a;
    PlaySettings s;
    s.pad_jump = "b";
    s.pad_attack = "a";
    s.pad_confirm = "b";
    s.pad_back = "back";
    s.pad_pause = "start";
    GameOptions go;
    REQUIRE(build_game_options(a, s, go).ok);
    CHECK(go.pad_back == "a");      // today's nintendo
    CHECK(go.pad_jump == "b");
    CHECK(go.pad_pause == "start");
}
