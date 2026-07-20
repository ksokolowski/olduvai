// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "presentation/report_templates.hpp"

using namespace olduvai::presentation;

TEST_CASE("templates + is_report_template detect the unedited state") {
    CHECK(is_report_template(""));
    CHECK(is_report_template(report_template("collision")));
    CHECK(is_report_template(report_template("visual")));
    CHECK(is_report_template(report_template("other")));
    CHECK_FALSE(is_report_template("I actually typed something"));
    CHECK(kReportTags.size() == 7);
    CHECK(kReportRepro.size() == 4);
    // collision differs from generic (so re-templating actually changes text)
    CHECK(report_template("collision") != report_template("other"));
}
