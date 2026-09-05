// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Numeric argument validation for the CLI surface.
//
// The CONFIG surface ignores a malformed number and keeps its default
// (test_options_resolve.cpp).  argv is the opposite case: the user typed it
// this run and meant it, so a value that cannot be honoured is an error with
// exit 2, not a silent fallback.  std::atoi could express neither — it returns
// 0 for "abc", and `--level abc` therefore booted the intro without a word.

#include "doctest/doctest.h"

#include <string>
#include <vector>

#include "app/cli_args.hpp"

using olduvai::app::CliArgs;
using olduvai::app::ParseOutcome;
using olduvai::app::PlaySettings;
using olduvai::app::parse_args;

namespace {

// parse_args takes char**, so the literals need writable backing storage.
struct Argv {
    std::vector<std::string> store;
    std::vector<char*> ptrs;
    explicit Argv(std::initializer_list<const char*> a) {
        store.reserve(a.size());
        for (const char* s : a) store.emplace_back(s);
        ptrs.reserve(store.size());
        for (auto& s : store) ptrs.push_back(s.data());
    }
    int argc() const { return static_cast<int>(ptrs.size()); }
    char** argv() { return ptrs.data(); }
};

ParseOutcome run(std::initializer_list<const char*> a, CliArgs& args,
                 PlaySettings& ps) {
    Argv v(a);
    return parse_args(v.argc(), v.argv(), args, ps);
}

}  // namespace

TEST_CASE("cli: a valid numeric argument parses and does not exit") {
    CliArgs a;
    PlaySettings ps;
    const auto out = run({"olduvai", "--level", "3", "--render-scale", "2"},
                         a, ps);
    CHECK(out.should_exit == false);
    CHECK(a.play_level == 3);
    CHECK(ps.render_scale == 2);
}

TEST_CASE("cli: --level with a non-numeric value is an error, not level 0") {
    // atoi("abc") == 0, which passes the existing 0..8 range check and means
    // "intro/title" — so the typo silently ran a different thing.
    CliArgs a;
    PlaySettings ps;
    const auto out = run({"olduvai", "--level", "abc"}, a, ps);
    CHECK(out.should_exit == true);
    CHECK(out.exit_code == 2);
}

TEST_CASE("cli: every numeric flag rejects a non-numeric value") {
    for (const char* flag : {"--level", "--render-scale", "--audio-rate",
                             "--audio-buffer", "--start-screen",
                             "--play-frames", "--play-shot-frame",
                             "--viewer-frames", "--render-audio-secs"}) {
        CAPTURE(flag);
        CliArgs a;
        PlaySettings ps;
        const auto out = run({"olduvai", flag, "abc"}, a, ps);
        CHECK(out.should_exit == true);
        CHECK(out.exit_code == 2);
    }
}

TEST_CASE("cli: a numeric flag rejects trailing garbage") {
    CliArgs a;
    PlaySettings ps;
    const auto out = run({"olduvai", "--render-scale", "2x"}, a, ps);
    CHECK(out.should_exit == true);
    CHECK(out.exit_code == 2);
}

TEST_CASE("cli: --window rejects a malformed geometry") {
    CliArgs a;
    PlaySettings ps;
    const auto bad = run({"olduvai", "--window", "640xabc"}, a, ps);
    CHECK(bad.should_exit == true);
    CHECK(bad.exit_code == 2);

    CliArgs b;
    PlaySettings qs;
    const auto missing_x = run({"olduvai", "--window", "640"}, b, qs);
    CHECK(missing_x.should_exit == true);
    CHECK(missing_x.exit_code == 2);
}

TEST_CASE("cli: --window accepts both x and X separators") {
    CliArgs a;
    PlaySettings ps;
    const auto out = run({"olduvai", "--window", "896X400"}, a, ps);
    CHECK(out.should_exit == false);
    CHECK(a.play_window_w == 896);
    CHECK(a.play_window_h == 400);
}

TEST_CASE("cli: --level still enforces its documented range") {
    CliArgs a;
    PlaySettings ps;
    const auto out = run({"olduvai", "--level", "9"}, a, ps);
    CHECK(out.should_exit == true);
    CHECK(out.exit_code == 2);
}

TEST_CASE("cli: --render-audio-secs accepts a fractional value") {
    CliArgs a;
    PlaySettings ps;
    const auto out = run({"olduvai", "--render-audio-secs", "1.5"}, a, ps);
    CHECK(out.should_exit == false);
    CHECK(a.render_audio_secs == doctest::Approx(1.5));
}
