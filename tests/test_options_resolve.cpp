// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Precedence matrix for the config-influenced play settings — defaults <
// play.json < --profile < CLI flags (options_resolve, CC3 phase 3).  The
// regression cases pin the exact 0.9.2 field bugs: the aspect guard, the
// first-run choice adoption, and the ask-once style_answered gating.

#include "doctest/doctest.h"

#include "app/config.hpp"
#include "app/options_resolve.hpp"

using olduvai::app::Config;
using olduvai::app::PlaySettings;
using olduvai::app::adopt_preset;
using olduvai::app::apply_profile;
using olduvai::app::merge_config;

TEST_CASE("options: empty config leaves every default in place") {
    PlaySettings s;
    merge_config(s, {});
    CHECK(s.enhanced == false);
    CHECK(s.aspect == "keep");
    CHECK(s.render_scale == 4);
    CHECK(s.music_device == "auto");
    CHECK(s.vga_scan == true);
    CHECK(s.style_answered == false);
    CHECK(s.config_game_dir == false);
}

TEST_CASE("options: config fills what the CLI left") {
    PlaySettings s;
    Config c = {{"enhanced", "true"},   {"hd_profile", "xbr"},
                {"render_scale", "2"},  {"aspect", "widescreen"},
                {"vga_scan", "false"},  {"autofire", "fast"},
                {"music_device", "opl"},{"game_dir", "/tmp/prehistorik"},
                {"pad_jump", "b"},      {"pad_deadzone", "5000"},
                {"audio_rate", "44100"},{"bug_report_dir", "/tmp/bugs"}};
    merge_config(s, c);
    CHECK(s.enhanced == true);
    CHECK(s.hd_profile == "xbr");
    CHECK(s.render_scale == 2);
    CHECK(s.aspect == "widescreen");
    CHECK(s.vga_scan == false);
    CHECK(s.autofire == "fast");
    CHECK(s.music_device == "opl");
    CHECK(s.game_dir == "/tmp/prehistorik");
    CHECK(s.config_game_dir == true);
    CHECK(s.pad_jump == "b");
    CHECK(s.pad_deadzone == 5000);
    CHECK(s.audio_rate == 44100);
    CHECK(s.bug_report_dir == "/tmp/bugs");
    CHECK(s.style_answered == true);   // config states the master flag
}

TEST_CASE("options: explicit --aspect keep beats a saved widescreen config "
          "(0.9.2 field-bug regression)") {
    PlaySettings s;
    s.aspect = "keep";        // the parse loop wrote the explicit value
    s.cli.aspect = true;      // …and flagged it as CLI-given
    merge_config(s, {{"aspect", "widescreen"}});
    CHECK(s.aspect == "keep");
}

TEST_CASE("options: CLI flags beat config on every flagged key") {
    PlaySettings s;
    s.enhanced = true;   s.cli.enhanced = true;
    s.hd_profile = "mmpx"; s.cli.hd = true;
    s.render_scale = 2;  s.cli.scale = true;
    s.vga_scan = false;  s.cli.vga_scan = true;
    s.autofire = "slow"; s.cli.autofire = true;
    s.game_dir = "/cli"; s.cli.game_dir = true;
    merge_config(s, {{"enhanced", "false"}, {"hd_profile", "xbr"},
                     {"render_scale", "4"}, {"vga_scan", "true"},
                     {"autofire", "off"},   {"game_dir", "/cfg"}});
    CHECK(s.enhanced == true);
    CHECK(s.hd_profile == "mmpx");
    CHECK(s.render_scale == 2);
    CHECK(s.vga_scan == false);
    CHECK(s.autofire == "slow");
    CHECK(s.game_dir == "/cli");
    CHECK(s.config_game_dir == false);
}

// These six keys are guarded on the CLI FLAG, not on the field's value.  The
// old form compared the field against its own default to infer "the user said
// nothing" — sound in the reference's argparse, where an unset value is None,
// and wrong here, where the parse loop writes real defaults.  Under the old
// form an explicit `--music-device auto` lost to a saved play.json and there
// was no way to ask for the default from the command line.
TEST_CASE("options: an unflagged key yields to the config") {
    PlaySettings s;
    merge_config(s, {{"music_device", "gm-builtin"},
                     {"display_mode", "cpu"}, {"transitions", "classic"},
                     {"sfx_backend", "opl"}, {"hd_font", "noto"},
                     {"banner_fx", "fire"}});
    CHECK(s.music_device == "gm-builtin");
    CHECK(s.display_mode == "cpu");
    CHECK(s.transitions == "classic");
    CHECK(s.sfx_backend == "opl");
    CHECK(s.hd_font == "noto");
    CHECK(s.banner_fx == "fire");
}

TEST_CASE("options: an explicit CLI value beats the config") {
    PlaySettings s;
    s.music_device = "opl";  s.cli.music_device = true;
    s.display_mode = "cpu";  s.cli.display_mode = true;
    merge_config(s, {{"music_device", "gm-builtin"}, {"display_mode", "gpu"}});
    CHECK(s.music_device == "opl");
    CHECK(s.display_mode == "cpu");
}

// The regression the old form could not express: the explicit value IS the
// default.  Every one of these was silently overridden before.
TEST_CASE("options: an explicit CLI value that equals the default still wins") {
    PlaySettings s;
    s.music_device = "auto";     s.cli.music_device = true;
    s.sfx_backend  = "auto";     s.cli.sfx_backend  = true;
    s.display_mode = "gpu";      s.cli.display_mode = true;
    s.transitions  = "smooth";   s.cli.transitions  = true;
    s.hd_font      = "freckle";  s.cli.hd_font      = true;
    s.banner_fx    = "caveman";  s.cli.banner_fx    = true;
    merge_config(s, {{"music_device", "gm-builtin"}, {"sfx_backend", "opl"},
                     {"display_mode", "cpu"},       {"transitions", "classic"},
                     {"hd_font", "noto"},           {"banner_fx", "fire"}});
    CHECK(s.music_device == "auto");
    CHECK(s.sfx_backend == "auto");
    CHECK(s.display_mode == "gpu");
    CHECK(s.transitions == "smooth");
    CHECK(s.hd_font == "freckle");
    CHECK(s.banner_fx == "caveman");
}

TEST_CASE("options: style_answered — the ask-once gate") {
    PlaySettings a;
    merge_config(a, {});
    CHECK(a.style_answered == false);       // virgin config → ask
    PlaySettings b;
    merge_config(b, {{"enhanced", "false"}});
    CHECK(b.style_answered == true);        // an explicit Classic counts
    PlaySettings c;
    c.cli.enhanced = true;
    merge_config(c, {});
    CHECK(c.style_answered == true);        // an explicit CLI flag counts
}

TEST_CASE("options: adopt_preset applies hd to THIS session "
          "(0.9.2 first-run regression)") {
    PlaySettings s;
    adopt_preset(s, /*cli_profile=*/"", "hd");
    CHECK(s.enhanced == true);
    CHECK(s.hd_profile == "omniscale");
    CHECK(s.render_scale == 4);
    CHECK(s.aspect == "widescreen");
}

TEST_CASE("options: adopt_preset dos clears the enhanced side") {
    PlaySettings s;
    s.enhanced = true;
    s.hd_profile = "omniscale";
    s.aspect = "widescreen";
    s.enhance_list = "hd-text";
    adopt_preset(s, "", "dos");
    CHECK(s.enhanced == false);
    CHECK(s.hd_profile == "native");
    CHECK(s.aspect == "keep");
    CHECK(s.enhance_list.empty());
}

TEST_CASE("options: adopt_preset respects CLI flags and --profile") {
    PlaySettings s;
    s.render_scale = 2;
    s.cli.scale = true;
    adopt_preset(s, "", "hd");
    CHECK(s.render_scale == 2);             // CLI-stated scale survives
    CHECK(s.enhanced == true);              // unflagged keys adopt
    PlaySettings t;
    adopt_preset(t, /*cli_profile=*/"dos", "hd");
    CHECK(t.enhanced == false);             // explicit --profile wins: no-op
    PlaySettings u;
    adopt_preset(u, "", "");
    CHECK(u.enhanced == false);             // empty preset (headless box) no-op
}

TEST_CASE("options: profile overlay flows through merge (dos beats saved hd)") {
    // The caller applies --profile onto the merged map before merge_config —
    // verify the documented config < profile ordering end to end.
    Config merged = {{"enhanced", "true"}, {"hd_profile", "omniscale"},
                     {"aspect", "widescreen"}};
    apply_profile(merged, "dos");
    PlaySettings s;
    merge_config(s, merged);
    CHECK(s.enhanced == false);
    CHECK(s.hd_profile == "native");
    CHECK(s.aspect == "keep");
    CHECK(s.style_answered == true);
}
