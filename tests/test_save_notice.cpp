// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Krzysztof Sokołowski
#include "doctest/doctest.h"

#include <cstdint>
#include <string>

#include "presentation/render/save_notice.hpp"

using olduvai::presentation::SaveNotice;

TEST_CASE("save notice: nothing until a save, then for kShowMs") {
    SaveNotice n;
    CHECK(n.text(0) == nullptr);
    CHECK(n.text(5000) == nullptr);
    n.post(true, 1000);
    CHECK(std::string(n.text(1000)) == "Saved");
    CHECK(std::string(n.text(1000 + SaveNotice::kShowMs - 1)) == "Saved");
    CHECK(n.text(1000 + SaveNotice::kShowMs) == nullptr);
}

TEST_CASE("save notice: a failed save says so; a new save restarts it") {
    SaveNotice n;
    n.post(false, 100);
    CHECK(std::string(n.text(200)) == "Save failed");
    CHECK_FALSE(n.saved());
    n.post(true, 1000);
    CHECK(std::string(n.text(1000 + SaveNotice::kShowMs - 1)) == "Saved");
}

TEST_CASE("save notice: the millisecond clock wrapping does not stick it") {
    SaveNotice n;
    const std::uint32_t near_wrap = 0xFFFFFF00u;
    n.post(true, near_wrap);
    CHECK(n.text(near_wrap + 10) != nullptr);              // across the wrap
    CHECK(n.text(near_wrap + SaveNotice::kShowMs) == nullptr);
}
