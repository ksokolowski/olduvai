// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
// Numeric validation for the presentation layer's debug hooks.
//
// A third policy alongside the two in test_cli_args / test_options_resolve:
// config keeps its default, CLI exits 2, and an env hook behaves as if it were
// never set.  The bug this replaces was `frame >= std::atoi(env)`, which for a
// mistyped value is `frame >= 0` — always true — so OLDUVAI_FORCE_WIN=<typo>
// won the boss fight on frame one and OLDUVAI_SMOOTH_FRAMES=<typo> ended the
// run on frame zero.  The quieter twin is `frame == std::atoi(env)`, which
// fires on frame 0 instead of never.

#include "doctest/doctest.h"

#include "env_shim.hpp"

#include <climits>
#include <cstdlib>
#include <string>

#include "presentation/env_num.hpp"

using olduvai::presentation::env_int;
using olduvai::presentation::parse_int;

TEST_CASE("env_num: a whole number parses") {
    int v = -999;
    REQUIRE(parse_int("42", v));
    CHECK(v == 42);
    REQUIRE(parse_int("-7", v));
    CHECK(v == -7);
    REQUIRE(parse_int("+3", v));
    CHECK(v == 3);
    REQUIRE(parse_int("0", v));
    CHECK(v == 0);
}

TEST_CASE("env_num: surrounding whitespace is tolerated") {
    int v = 0;
    REQUIRE(parse_int("  12  ", v));
    CHECK(v == 12);
}

TEST_CASE("env_num: a non-number is rejected and leaves the output alone") {
    int v = 1234;
    CHECK_FALSE(parse_int("abc", v));
    CHECK(v == 1234);          // untouched, so callers can pre-seed a default
    CHECK_FALSE(parse_int("", v));
    CHECK(v == 1234);
    CHECK_FALSE(parse_int(nullptr, v));
    CHECK(v == 1234);
}

TEST_CASE("env_num: trailing garbage is rejected, not truncated") {
    // The atoi behaviour this exists to stop: "2x" was silently 2.
    int v = 0;
    CHECK_FALSE(parse_int("2x", v));
    CHECK_FALSE(parse_int("12 34", v));
    CHECK_FALSE(parse_int("1.5", v));
}

TEST_CASE("env_num: out-of-range values are rejected") {
    int v = 0;
    CHECK_FALSE(parse_int("99999999999999999999", v));
    CHECK_FALSE(parse_int("-99999999999999999999", v));
}

TEST_CASE("env_int: an unset variable yields the fallback") {
    // A name nothing sets; unsetenv guards against a polluted environment.
    olduvai_test::unset_env("OLDUVAI_TEST_ENV_NUM");
    CHECK(env_int("OLDUVAI_TEST_ENV_NUM", -1) == -1);
    CHECK(env_int("OLDUVAI_TEST_ENV_NUM", INT_MAX) == INT_MAX);
}

TEST_CASE("env_int: a set variable is honoured") {
    olduvai_test::set_env("OLDUVAI_TEST_ENV_NUM", "17");
    CHECK(env_int("OLDUVAI_TEST_ENV_NUM", -1) == 17);
    olduvai_test::unset_env("OLDUVAI_TEST_ENV_NUM");
}

TEST_CASE("env_int: a mistyped variable is indistinguishable from unset") {
    // The whole point. With atoi these returned 0, and 0 is a frame number:
    // `frame >= 0` fires immediately and `frame == 0` fires once, early.
    olduvai_test::set_env("OLDUVAI_TEST_ENV_NUM", "abc");
    CHECK(env_int("OLDUVAI_TEST_ENV_NUM", -1) == -1);
    CHECK(env_int("OLDUVAI_TEST_ENV_NUM", INT_MAX) == INT_MAX);
    olduvai_test::set_env("OLDUVAI_TEST_ENV_NUM", "3frames");
    CHECK(env_int("OLDUVAI_TEST_ENV_NUM", -1) == -1);
    olduvai_test::unset_env("OLDUVAI_TEST_ENV_NUM");
}
